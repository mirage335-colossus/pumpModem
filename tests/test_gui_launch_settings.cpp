#include "application.hpp"
#include "controller.hpp"
#include "launch_command.hpp"
#include "fhss_profile.hpp"
#include "datapump/tuning.hpp"
#include "datapump/sched_hop.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>

using namespace datapump;
using namespace datapump::gui;
namespace {
using F=ui::Field;
using C=ui::Command;
void check(bool value,const char* message) {if(!value)throw Error(message);}
void near(double a,double b,const char* message) {
    check(std::isfinite(a)&&std::abs(a-b)<1e-10*std::max(1.,std::abs(b)),message);
}
void load(Controller& controller,const std::string& command) {
    controller.edit(F::planner_command,command);
    controller.activate(C::planner_load_command);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(controller.field(F::key_path).text=="Loading key entries..."&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.field(F::key_path).text!="Loading key entries...","Parameter-list key loading did not finish");
}
void load_synthetic_key(Controller& controller) {
    struct Fixture {
        std::filesystem::path dir=std::filesystem::temp_directory_path()/"dp-fhss-list-20261011",path=dir/"synthetic-key.bin";
        Fixture(){std::filesystem::create_directories(dir);create_keyring(path,{"FHSS synthetic key"});}
        ~Fixture(){std::error_code ec;std::filesystem::remove(path,ec);std::filesystem::remove(dir,ec);}
    };
    static Fixture fixture;
    controller.activate(C::open_keyfile);const auto services=controller.take_services();
    check(services.size()==1,"Synthetic key chooser missing");
    controller.complete_service({services.front().id,false,fixture.path.string(),{}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!controller.settings().transfer.key&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.settings().transfer.key.has_value(),"Synthetic FHSS key load failed");
}
void exact_scheduled_target_import() {
  for(const unsigned multiplier:{3U,5U}) {
    const auto trace_path=std::filesystem::temp_directory_path()/
        ("dp-exact-scheduled-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    auto trace=std::make_unique<audio::trace::Scope>(trace_path);
    Controller controller({true,false});load_synthetic_key(controller);
    controller.edit(F::binary,"001");
    const auto keys=launch_command::parse(controller.field(F::planner_command).text);
    check(keys.keyfile&&keys.key_name,"Synthetic named key export missing");
    auto requested=launch_command::parse(
        "--pattern auto-keystream --tx-dbm 36.020599913279625 --path-loss-db 180 --noise-dbm-hz -164 "
        "--oscillator gpsdo-xo --rf-oscillator gpsdo-xo --shift 3e7 --search-margin 3 --reference independent "
        "--target-snr 8 --rate 36 --carrier 30001500 --doppler 0c --dsp-workspace 50% "
        "--clock-sync default --audio-error 0ms --dsss-mode interleave --dsss-factor 10 --dsss-version interleave "
        "--search-arithmetic default --receiver-strategy auto --receiver-cpu-budget .6 --live-duplex yes "
        "--sched-hop-dilution 0 --sched-hop 5 --sched-frequency-search 1 --fhss off --tx-key named");
    requested.keyfile=keys.keyfile;requested.key_name=keys.key_name;
    requested.sched_hop=multiplier;
    const auto command=launch_command::format(requested);
    const auto verify=[&](Controller& c) {
        near(std::stod(c.field(F::snr).text),8,"Explicit scheduled command changed the requested short target");
        near(std::stod(c.field(F::long_snr).text),8,"Explicit scheduled command changed the requested long target");
        near(c.link_plan()->inputs.target_db_hz,8,"Explicit scheduled command changed planner target");
        near(c.settings().transfer.modem.bandwidth_hz,36,"Explicit scheduled command failed to install Rate");
        check(c.settings().transfer.key&&c.settings().transfer.modem.scheduled_hopping&&
              c.settings().transfer.modem.scheduled_hopping->time_multiplier==multiplier&&
              c.settings().transfer.modem.dsss_factor==10&&c.settings().transfer.modem.scramble,
              "Explicit scheduled command failed to commit the key/calendar/spreading atomically");
        const auto expected=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
            modem::OuterDsssVersion::interleaved_v2);
        check(modem::symbol_sample_count(c.settings().transfer.modem)==modem::symbol_sample_count(expected.config)&&
              c.settings().long_message_modem&&modem::symbol_sample_count(*c.settings().long_message_modem)==modem::symbol_sample_count(expected.config),
              "Explicit scheduled target was silently fitted to another transmitted waveform");
        const auto saved=launch_command::parse(c.field(F::planner_command).text);
        check(saved.target_db_hz==8&&saved.short_target_db_hz.value_or(*saved.target_db_hz)==8&&
              saved.long_target_db_hz.value_or(*saved.target_db_hz)==8&&
              saved.sched_hop==multiplier,"Scheduled launch export lost exact target/calendar");
    };
    load(controller,command);verify(controller);
    check(controller.field(F::short_bits).text=="001"&&!controller.snapshot().transmitting,
          "Loading scheduled settings changed or transmitted the draft");
    Controller startup({false,false,requested});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(startup.field(F::key_path).text=="Loading key entries..."&&std::chrono::steady_clock::now()<deadline) {
        startup.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    verify(startup);startup.close();controller.close();trace.reset();
    std::ifstream in(trace_path);const std::string records((std::istreambuf_iterator<char>(in)),{});
    check(records.find("\"event\":\"gui_settings_committed\"")!=std::string::npos&&
          records.find("\"short_target_db_hz\":8")!=std::string::npos&&
          records.find("\"long_target_db_hz\":8")!=std::string::npos,
          "Verbose import trace failed to expose the actually committed targets");
    std::filesystem::remove(trace_path);
  }
}
void generated_and_pasted_settings() {
    Controller controller({true,true});
    const auto original=controller.field(F::planner_command).text;
    const auto initial=launch_command::parse(original);
    check(initial.pattern=="auto-pattern"&&initial.target_db_hz&&
        initial.tx_dbm&&initial.path_loss_db&&initial.noise_dbm_hz&&
        initial.oscillator&&!initial.rf_oscillator&&initial.rf_shift_hz==0&&initial.search_margin==3&&initial.reference=="independent"&&
        initial.rate_hz&&initial.carrier_hz&&initial.workspace_percent,
        "Launch command must contain every shareable planner setting");
    near(*initial.target_db_hz,controller.link_plan()->inputs.target_db_hz,"Command did not use planner preview target");
    check(original.find("--simulation")==std::string::npos,"Shareable command must launch with Simulation No");
    controller.edit(F::binary,"001");
    const std::string pasted="\"C:\\Program Files\\Data Pump\\datapump-gui.exe\"\n"
        "--auto-pattern --tx-dbm 36 --path-loss-db 180 --noise-dbm-hz -170 "
        "--oscillator gpsdo-ocxo --target-snr -8 --rate 1200 --carrier 900 --dsp-workspace 25%";
    controller.edit(F::planner_command,pasted);
    controller.poll();
    check(controller.field(F::planner_command).text==pasted,"Ordinary polling erased a pasted command");
    check(controller.settings().simulation,"Pasting alone changed Simulation");
    near(controller.settings().transfer.modem.bandwidth_hz,3600,"Pasting applied a setting before Load");
    controller.activate(C::planner_load_command);
    check(!controller.settings().simulation&&controller.field(F::simulation).selected=="no","Load must switch Simulation to No");
    near(controller.settings().transfer.modem.bandwidth_hz,1200,"Load missed rate");
    near(controller.settings().transfer.modem.carrier_hz,900,"Load missed carrier");
    check(controller.field(F::simulation_oscillator).selected=="gpsdo-ocxo","Load missed oscillator model");
    check(controller.field(F::dsp_workspace).selected=="ram-25","Load missed DSP workspace");
    const auto model=controller.link_plan();
    near(model->inputs.tx_dbm,36,"Load missed TX power");
    near(model->inputs.path_loss_db,180,"Load missed path loss");
    near(model->inputs.noise_density_dbm_hz,-170,"Load missed receiver noise");
    check(controller.settings().long_message_modem.has_value(),"Load removed long-message profile");
    near(modem::symbol_seconds(controller.settings().transfer.modem),
         modem::symbol_seconds(*controller.settings().long_message_modem),"Common target did not apply to short and long profiles");
    check(controller.field(F::short_bits).text=="001","Loading settings changed exact raw draft bits");
    check(!controller.snapshot().transmitting,"Loading settings started a transmission");
    check(controller.field(F::planner_command).text!=pasted,"Successful Load did not normalize the command");

    const auto accepted=launch_command::parse(controller.field(F::planner_command).text);
    near(*accepted.target_db_hz,model->inputs.target_db_hz,"Normalized command lost accepted target");
    const auto symbol=modem::symbol_seconds(controller.settings().transfer.modem);
    load(controller,controller.field(F::planner_command).text);
    near(modem::symbol_seconds(controller.settings().transfer.modem),symbol,"Command round trip changed sample timing");

    controller.edit(F::planner_target,"23");controller.commit_target(F::planner_target);
    near(*launch_command::parse(controller.field(F::planner_command).text).target_db_hz,
         controller.link_plan()->inputs.target_db_hz,"Planner target edit did not refresh launch command");
    controller.edit(F::link_power,"100 mW");
    near(*launch_command::parse(controller.field(F::planner_command).text).tx_dbm,20,"Power edit did not refresh launch command");
    controller.select(F::dsp_workspace,"ram-75");
    check(launch_command::parse(controller.field(F::planner_command).text).workspace_percent==75,
          "Workspace selection did not refresh command");
    controller.select(F::simulation,"yes");
    const auto before=controller.field(F::planner_command).text;
    check(before.find("--simulation")==std::string::npos,"Simulation selection leaked into launch command");
    controller.close();
}
void rejected_settings_remain_atomic() {
    Controller controller;
    const auto initial=controller.field(F::planner_command).text;
    const auto rate=controller.settings().transfer.modem.bandwidth_hz;
    const auto target=controller.link_plan()->inputs.target_db_hz;
    for(const auto& invalid:{"./datapump-gui --rate 100 --unknown flag",
        "./datapump-gui --tx-dbm 50 --carrier 0",
        "./datapump-gui --rate 100 --dsp-workspace 60%",
        "./datapump-gui --tx-dbm NaN", "./datapump-gui --target-snr"}) {
        load(controller,invalid);
        near(controller.settings().transfer.modem.bandwidth_hz,rate,"Invalid import partially changed rate");
        near(controller.link_plan()->inputs.target_db_hz,target,"Invalid import partially changed target");
        check(controller.field(F::planner_command).text==invalid,"Invalid pasted command was discarded");
        check(controller.field(F::status).text!="Settings loaded · Simulation No","Invalid import reported success");
    }
    load(controller,initial);
    check(controller.field(F::status).text=="Settings loaded · Simulation No","Valid command could not recover after invalid paste");
    const auto previous_rate=controller.settings().transfer.modem.bandwidth_hz;
    load(controller,"--tx-dbm 20");
    near(controller.link_plan()->inputs.tx_dbm,20,"Flags-only partial import failed");
    near(controller.settings().transfer.modem.bandwidth_hz,previous_rate,"Partial import changed an omitted rate");
    load(controller,"--target-snr -8 --short-target-snr 32 --long-target-snr 55");
    near(controller.link_plan()->inputs.target_db_hz,-8,"Explicit TX override replaced independent preview target");
    near(std::stod(controller.field(F::snr).text),32,"Short target override was ignored");
    near(std::stod(controller.field(F::long_snr).text),55,"Long target override was ignored");
    const auto independent=controller.field(F::planner_command).text;
    const auto exported=launch_command::parse(independent);
    check(exported.target_db_hz==-8&&exported.short_target_db_hz==32&&exported.long_target_db_hz==55,
          "Launch export lost independent short/long targets");
    load(controller,independent);
    near(std::stod(controller.field(F::snr).text),32,"Reload replaced independent short target");
    near(std::stod(controller.field(F::long_snr).text),55,"Reload replaced independent long target");
    load(controller,"--short-target-snr 23");
    near(controller.link_plan()->inputs.target_db_hz,-8,"Short-only import changed an omitted preview target");
    controller.close();
}
void startup_and_submit_behavior() {
    auto patch=launch_command::parse("--auto-pattern --rate 2400 --carrier 1500 --target-snr 32 "
        "--oscillator gpsdo-tcxo --tx-dbm 0 --path-loss-db 170 --noise-dbm-hz -164 --dsp-workspace 50%");
    Controller controller({false,false,patch});
    near(controller.settings().transfer.modem.bandwidth_hz,2400,"Startup ignored launch settings");
    check(controller.field(F::simulation_oscillator).selected=="gpsdo-tcxo","Startup missed oscillator");
    check(!controller.settings().simulation,"Launch settings implicitly enabled simulation");
    controller.close();
    Controller simulated({true,false,patch});
    check(simulated.settings().simulation,"Startup lost explicit --simulation compatibility");
    simulated.close();

    Launch launch;launch.page=ui::Page::planner;
    Application app(launch);
    const auto& screen=ui::console_screen();
    const auto found=std::find_if(screen.begin(),screen.end(),[](const auto& c){return c.field==F::planner_command;});
    check(found!=screen.end()&&found->document_only&&found->multiline,"Launch command must be an inline multiline control");
    check(found->submit==C::none,"Enter in launch command could dispatch transmission or load");
    check(!app.submit(*found,false,false),"Command editor Enter must remain editable text");
    app.close();
}
void narrow_rate_round_trips() {
    Controller controller({true,true});controller.edit(F::binary,"001");
    for(const auto& command:{"--rate .001 --carrier .0005 --oscillator gpsdo-xo --target-snr 32",
                            "--rate 10 --carrier 5 --oscillator gpsdo-xo --target-snr 32"}) {
        const auto requested=launch_command::parse(command);
        load(controller,command);
        check(!controller.settings().simulation&&controller.field(F::short_bits).text=="001"&&
              !controller.snapshot().transmitting,"New Rate preset loading must preserve exact bits and Simulation No");
        near(controller.settings().transfer.modem.bandwidth_hz,*requested.rate_hz,"New Rate preset did not load");
        near(controller.settings().transfer.modem.carrier_hz,*requested.carrier_hz,"New Rate center carrier did not load");
        const auto exported=controller.field(F::planner_command).text;
        const auto patch=launch_command::parse(exported);
        check(patch.rate_hz==requested.rate_hz&&patch.carrier_hz==requested.carrier_hz,
              "Generated launch command lost the new Rate or its fractional carrier");
        const auto samples=modem::symbol_sample_count(controller.settings().transfer.modem);
        load(controller,exported);
        check(controller.field(F::planner_command).text==exported&&
              modem::symbol_sample_count(controller.settings().transfer.modem)==samples&&
              controller.field(F::short_bits).text=="001","New Rate launch/load roundtrip changed sampled geometry or wire bits");
    }
    controller.close();
}
void live_validation_is_atomic() {
    Controller controller({true,true});controller.start();controller.poll();
    const auto rate=controller.settings().transfer.modem.bandwidth_hz;
    const auto loss=controller.link_plan()->inputs.path_loss_db;
    for(const auto& invalid:{"--rate 1000000 --carrier 750000 --target-snr 32",
                            "--tx-dbm -200 --path-loss-db 500 --noise-dbm-hz 0"}) {
        load(controller,invalid);
        check(controller.settings().simulation&&controller.field(F::simulation).selected=="yes",
              "Receiver validation failure partially disabled simulation");
        near(controller.settings().transfer.modem.bandwidth_hz,rate,"Receiver validation failure partially changed rate");
        near(controller.link_plan()->inputs.path_loss_db,loss,"Receiver validation failure partially changed path loss");
        check(controller.field(F::planner_command).text==invalid,"Receiver validation failure discarded pasted input");
    }
    controller.close();
}
}
void real_radio_configuration() {
    Controller controller;
    const auto tone=controller.settings().transfer.modem.carrier_hz;
    load(controller,"--carrier 10.0015MHz --shift 10MHz --rf-oscillator gpsdo-ocxo --reference shared-radio --search-margin 3x");
    const auto policy=*controller.settings().transfer.modem.oscillator_search;
    check(policy.reference==modem::OscillatorReference::shared_radio&&policy.rf_shift_hz==10000000&&policy.margin==3,
        "Radio controls did not reach the actual receiver policy");
    near(controller.settings().transfer.modem.carrier_hz,tone,"RF configuration changed the nonzero PCM carrier");
    near(controller.settings().simulation_clock_error_ppm,.0001,"Shared radio incorrectly retained the independent sound-card clock");
    near(controller.settings().simulation_frequency_offset_hz,.001,"Shared radio applied the RF conversion error more than once");
    const auto canonical=controller.field(F::planner_command).text;
    check(launch_command::parse(canonical).rf_shift_hz==10000000,"Canonical command lost RF LO");
    load(controller,"--lf-reference 0 --rf-carrier 10.0015MHz");
    check(*controller.settings().transfer.modem.oscillator_search==policy,
        "Absolute carrier alias changed receiver policy");
    check(controller.field(F::simulation_oscillator).selected=="gpsdo-ocxo"&&
          controller.field(F::rf_oscillator).selected=="baseband-clock",
          "Legacy shared-radio import did not expose its effective common model");
    controller.select(F::rf_oscillator,"gpsdo-tcxo");
    controller.select(F::simulation_oscillator,"crystal");
    near(controller.settings().simulation_clock_error_ppm,100,"Independent Baseband clock silently inherited Shift GPS discipline");
    controller.select(F::rf_oscillator,"gpsdo-xo");
    near(controller.settings().simulation_clock_error_ppm,100,"Changing RF oscillator changed independent ADC/DAC uncertainty");
    load(controller,"--carrier 1500 --shift 0");
    near(controller.settings().simulation_frequency_offset_hz,0,"Untranslated audio retained inactive RF conversion error");
    near(controller.settings().simulation_phase_noise_degrees_per_sqrt_second,.5,"Untranslated audio retained inactive RF phase diffusion");
    controller.close();
}
void invalid_shift_edit_recovers() {
    Controller controller({true,true});
    const auto accepted=controller.settings().transfer.modem;
    controller.edit(F::rf_shift,"1 MHz");
    check(controller.settings().transfer.modem.carrier_hz==accepted.carrier_hz&&
          controller.settings().transfer.modem.oscillator_search==accepted.oscillator_search&&
          controller.field(F::rf_shift).text=="1 MHz"&&
          controller.field(F::status).text.find("Carrier must be greater than Shift")!=std::string::npos,
          "An invalid Shift edit must preserve accepted geometry and expose a correctable error");
    controller.edit(F::carrier,"1.0015 MHz");
    check(controller.settings().transfer.modem.carrier_hz==1500&&
          controller.settings().transfer.modem.oscillator_search->rf_shift_hz==1000000,
          "Correcting Carrier must recover the originally rejected Shift edit");
    controller.close();
}
void absolute_carrier_and_shift() {
    Controller controller({true,true});
    load(controller,"--carrier 1.0015MHz --shift 1MHz --oscillator gpsdo-ocxo --rf-oscillator gpsdo-ocxo --reference shared-radio");
    const auto translated=controller.settings().transfer.modem;
    near(translated.carrier_hz,1500,"Absolute Carrier minus Shift did not produce the stream tone");
    check(translated.sample_rate==tuning::recommended_sample_rate(3600,1500),"Translated carrier incorrectly raised the stream sample rate");
    near(translated.oscillator_search->rf_shift_hz,1000000,"Shift was not retained by oscillator search");
    check(translated.oscillator_search->sideband==modem::OscillatorSideband::upper,"Receiver was not fixed to USB");
    auto command=launch_command::parse(controller.field(F::planner_command).text);
    near(*command.carrier_hz,1001500,"Export wrote the internal stream tone instead of Carrier");
    near(*command.rf_shift_hz,1000000,"Export lost Shift");
    check(controller.field(F::carrier).text=="1.0015 MHz","Carrier field did not retain absolute frequency");
    check(controller.field(F::carrier).options.front().id=="1.0015 MHz","Carrier presets ignored Shift");
    load(controller,controller.field(F::planner_command).text);
    near(controller.settings().transfer.modem.carrier_hz,1500,"Round trip subtracted Shift twice");
    controller.edit(F::rf_shift,"0 Hz");
    near(controller.settings().transfer.modem.carrier_hz,1001500,"Direct real stream did not use absolute Carrier");
    check(controller.settings().transfer.modem.sample_rate>=4006000,"Direct real stream sample rate aliases Carrier");
    controller.edit(F::rf_shift,"1 MHz");
    near(controller.settings().transfer.modem.carrier_hz,1500,"Shift edit did not restore audio stream");
    controller.edit(F::bandwidth,"100 Hz");
    near(controller.settings().transfer.modem.carrier_hz,tuning::recommended_carrier_hz(100),"Rate edit lost translated stream default");
    check(controller.field(F::carrier).options.front().id==controller.field(F::carrier).text,"Rate edit did not select an absolute carrier preset");
    const auto accepted=controller.settings().transfer.modem;
    controller.edit(F::carrier,"1 MHz");
    near(controller.settings().transfer.modem.carrier_hz,accepted.carrier_hz,"Invalid Carrier committed a zero stream tone");
    check(controller.field(F::status).text.find("Carrier must be greater than Shift")!=std::string::npos,"Invalid difference did not give a useful error");
    controller.edit(F::carrier,"1.00005 MHz");
    near(controller.settings().transfer.modem.carrier_hz,50,"Corrected Carrier did not recover from invalid edit");
    load(controller,"--carrier 1001500.0001234567 --shift 1000000.0001234567");
    controller.edit(F::bandwidth,"120 Hz");
    check(controller.settings().transfer.modem.carrier_hz==1500,"Rate preset rounded the fractional Shift and changed the stream tone");
    controller.edit(F::carrier,controller.field(F::carrier).options.back().id);
    check(controller.settings().transfer.modem.carrier_hz==60,"Center preset rounded a fractional Shift");
    const auto fractional=controller.field(F::planner_command).text;
    load(controller,fractional);
    check(controller.settings().transfer.modem.carrier_hz==60,"Fractional Carrier/Shift export changed the stream tone");
    controller.close();
}
void zero_shift_legacy_and_shared_round_trip() {
    Controller controller({true,true});
    load(controller,"--carrier 1.5kHz --shift 0Hz --oscillator crystal --rf-oscillator gpsdo-ocxo --reference shared-radio");
    check(controller.field(F::simulation_oscillator).selected=="crystal"&&
          controller.field(F::rf_oscillator).selected=="baseband-clock"&&
          controller.field(F::rf_oscillator).display_text=="N/A"&&!controller.field(F::rf_oscillator).enabled,
          "Zero-Shift legacy load replaced the Baseband model with an inactive Shift model");
    const auto zero=*controller.settings().transfer.modem.oscillator_search;
    check(zero.reference==modem::OscillatorReference::independent_audio&&zero.rf==modem::OscillatorModel{0,0}&&
          controller.settings().simulation_clock_error_ppm==100&&
          controller.settings().simulation_phase_noise_degrees_per_sqrt_second==.5&&
          controller.settings().simulation_frequency_offset_hz==0,
          "Zero Shift retained modeled conversion accuracy or phase drift");
    auto command=launch_command::parse(controller.field(F::planner_command).text);
    check(command.oscillator=="crystal"&&!command.rf_oscillator&&command.reference=="independent"&&command.rf_shift_hz==0,
          "Zero-Shift export retained hidden effective RF settings");
    load(controller,controller.field(F::planner_command).text);
    check(controller.field(F::rf_oscillator).selected=="baseband-clock",
          "A zero-Shift command round trip discarded the remembered shared clock choice");
    load(controller,"--carrier 2.4000015GHz --shift 2.4GHz --oscillator crystal --rf-oscillator gpsdo-tcxo --lf-reference 0");
    check(controller.field(F::carrier).text=="2.4000015 GHz"&&controller.field(F::rf_shift).text=="2.4 GHz"&&
          controller.field(F::simulation_oscillator).selected=="gpsdo-tcxo"&&
          controller.field(F::rf_oscillator).selected=="baseband-clock",
          "GHz legacy shared clock load did not expose effective Carrier, Shift and Baseband model");
    load(controller,"--oscillator gpsdo-ocxo --reference shared-radio");
    check(controller.field(F::simulation_oscillator).selected=="gpsdo-ocxo",
          "Partial shared-clock import discarded an explicit Baseband model");
    const auto shared=*controller.settings().transfer.modem.oscillator_search;
    check(shared.reference==modem::OscillatorReference::shared_radio&&shared.lf==shared.rf,
          "Changing a shared Baseband oscillator left stale Shift parameters");
    command=launch_command::parse(controller.field(F::planner_command).text);
    check(command.oscillator=="gpsdo-ocxo"&&command.rf_oscillator=="gpsdo-ocxo"&&command.reference=="shared-radio",
          "Shared clock export did not use the visible effective Baseband oscillator");
    load(controller,controller.field(F::planner_command).text);
    check(*controller.settings().transfer.modem.oscillator_search==shared,
          "Shared clock export/import changed the modeled receiver search");
    controller.close();
}
void clock_and_spread_controls() {
    Controller controller({true,true});
    check(std::none_of(controller.field(F::clock_accuracy).options.begin(),controller.field(F::clock_accuracy).options.end(),
        [](const auto& option){return option.id.starts_with("GPS_")||option.label.starts_with("GPS_");}),
        "Clock sync menu must offer accuracy durations instead of composite serialized policies");
    check(!controller.settings().transfer.clock_sync && controller.field(F::clock_accuracy).text=="Default" &&
          controller.field(F::clock_region).text=="Default" && controller.field(F::clock_offset).text=="Default",
          "UTC controls must preserve the existing default search");
    const auto oscillator=controller.settings().transfer.modem.oscillator_search;
    check(controller.field(F::audio_error).text=="Default" && controller.settings().transfer.audio_timing_error_seconds==.07&&
        controller.field(F::audio_error).options.front().id=="Default"&&
        std::any_of(controller.field(F::audio_error).options.begin(),controller.field(F::audio_error).options.end(),
            [](const auto& option){return option.id=="0ms";}),
        "audio timing needs a 70 ms Default and an independently editable explicit zero allowance");
    controller.edit(F::clock_accuracy,"0.1ms");
    check(controller.field(F::clock_region).text=="1ms"&&controller.field(F::clock_offset).text=="0ms"&&
        controller.settings().transfer.clock_sync->region_seconds==.001&&
        controller.settings().transfer.clock_sync->offset_seconds==0&&
        controller.settings().transfer.audio_timing_error_seconds==.07 && controller.field(F::audio_error).text=="70ms",
        "initial custom clock selection must start at a one-ms region, zero offset and 70 ms audio allowance");
    controller.edit(F::clock_accuracy,"Default");
    controller.edit(F::audio_error,"12345us");
    check(controller.settings().transfer.audio_timing_error_seconds==.012345 &&
        !controller.settings().transfer.clock_sync && controller.settings().transfer.modem.oscillator_search==oscillator,
        "editing audio error changed GPS/oscillator mode or lost precision");
    for(const auto reset:{F::clock_accuracy,F::clock_region,F::clock_offset,F::audio_error}) {
        controller.edit(F::clock_accuracy,"GPS_0.1ms-400ms_region-2564ms_offset");
        controller.edit(F::audio_error,"12345us");
        const auto policy=controller.settings().transfer.clock_sync;
        check(policy && policy->accuracy_seconds==.0001 && policy->region_seconds==.4 && policy->offset_seconds==2.564,
              "clock preset did not atomically set all three independent controls");
        check(controller.field(F::clock_accuracy).text=="0.1ms"&&controller.field(F::clock_region).text=="400ms"&&
            controller.field(F::clock_offset).text=="2564ms","serialized policy leaked into an individual duration editor");
        check(controller.settings().transfer.modem.oscillator_search==oscillator,
              "GPS PC timing changed oscillator assumptions");
        controller.edit(reset,"Default");
        check(!controller.settings().transfer.clock_sync && controller.field(F::clock_accuracy).text=="Default" &&
              controller.field(F::clock_region).text=="Default" && controller.field(F::clock_offset).text=="Default",
              "Default in any timing editor must reset the complete clock triplet");
        check(controller.settings().transfer.audio_timing_error_seconds==.07 && controller.field(F::audio_error).text=="Default",
            "Default timing reset did not reset Audio error");
        load(controller,controller.field(F::planner_command).text);
        check(!controller.settings().transfer.clock_sync && controller.field(F::audio_error).text=="Default" &&
            controller.settings().transfer.audio_timing_error_seconds==.07,
            "Default timing launch round-trip changed Audio error");
    }
    for(const auto duration:{"0ms","30ms","50ms"}) {
        load(controller,std::string("--clock-sync default --audio-error ")+duration);
        const auto allowance=clock_sync::duration(duration);
        check(!controller.settings().transfer.clock_sync && controller.field(F::audio_error).text==duration &&
            controller.settings().transfer.audio_timing_error_seconds==allowance,"older explicit audio setting was discarded");
        load(controller,"--clock-sync default");
        check(controller.settings().transfer.audio_timing_error_seconds==allowance,
            "partial clock import erased omitted audio allowance");
        controller.edit(F::clock_accuracy,"1ms");
        check(controller.settings().transfer.audio_timing_error_seconds==allowance,
            "enabling custom clocks overwrote an explicit audio allowance");
    }
    controller.edit(F::clock_accuracy,"100ns");controller.edit(F::clock_region,"1us");controller.edit(F::clock_offset,"0ms");
    controller.edit(F::audio_error,"12345us");
    const auto accepted=controller.settings().transfer.clock_sync;
    const auto command=controller.field(F::planner_command).text;
    for(const auto* invalid:{"NaNms","-1ms","61s"}) {
        controller.edit(F::audio_error,invalid);
        check(controller.settings().transfer.audio_timing_error_seconds==.012345 &&
            controller.field(F::planner_command).text==command,
            "invalid audio edit changed live settings or exported command");
    }
    controller.edit(F::clock_region,"NaNms");
    check(controller.settings().transfer.clock_sync==accepted && controller.field(F::planner_command).text==command,
          "invalid clock edit partially changed live timing or export");
    check(controller.field(F::dsss_version).options.size()==2&&
        controller.field(F::dsss_version).options[0].label=="Off"&&controller.field(F::dsss_version).options[1].label=="Interleave",
        "public DSSS mode must be exactly Off and Interleave");
    check(!controller.field(F::dsss_version).enabled&&!controller.field(F::dsss_factor).enabled&&!controller.field(F::fhss).enabled,
        "Spreading controls enabled without encryption");
    controller.select(F::dsss_version,"interleave");controller.select(F::fhss,"fake-fcc");
    check(controller.field(F::dsss_version).selected=="off"&&controller.field(F::fhss).selected=="off","Unkeyed callback enabled spreading");
    load(controller,"--dsss-mode interleave --dsss-factor 10 --rate 360 --carrier 1500");
    near(controller.settings().transfer.modem.bandwidth_hz,360,"direct Interleave activation did not install its usable voice pair");
    load(controller,"--dsss-factor 100 --rate 36 --carrier 1500");
    check(controller.field(F::dsss_factor).selected=="100" && controller.settings().transfer.modem.dsss_factor==1 &&
          controller.field(F::dsss_factor).display_text.find("key needed")!=std::string::npos,
          "outer DSSS must remain inactive and labeled when no private key is selected");
    near(controller.settings().transfer.modem.bandwidth_hz,36,"DSSS 100x did not install its voice-passband Rate");
    near(controller.settings().transfer.modem.carrier_hz,1500,"DSSS default did not retain a 1.5 kHz stream carrier");
    check(controller.field(F::dsss_version).selected=="interleave"&&
        controller.settings().transfer.modem.outer_dsss_version==datapump::modem::OuterDsssVersion::legacy_v1,
        "new DSSS selection must default to v2 without changing the unkeyed Off waveform");
    load(controller,"--dsss-mode off");
    const auto off_saved=launch_command::parse(controller.field(F::planner_command).text);
    check(off_saved.dsss_mode=="off"&&off_saved.dsss_factor==100,"Off export lost remembered spreading");
    Controller off_copy({false,false,off_saved});
    check(off_copy.field(F::dsss_version).selected=="off"&&off_copy.field(F::dsss_factor).selected=="100"&&
        off_copy.settings().transfer.modem.dsss_factor==1,"saved Off mode activated spreading or lost its factor");off_copy.close();
    load(controller,"--dsss-mode interleave");
    load(controller,"--dsss-version legacy");
    near(controller.settings().transfer.modem.bandwidth_hz,36,"version-only selection reset the entered Rate");
    near(controller.settings().transfer.modem.carrier_hz,1500,"version-only selection reset Carrier");
    check(controller.field(F::dsss_factor).selected=="100"&&
        launch_command::parse(controller.field(F::planner_command).text).dsss_version=="legacy",
        "legacy diagnostic choice changed DSSS factor or was omitted from export");
    load_synthetic_key(controller);
    // Isolate the original narrow FHSS geometry from the remembered 100x mode.
    controller.select(F::dsss_version,"off");
    controller.select(F::fhss,"fake-fcc");
    const auto model=controller.link_plan();
    check(controller.settings().transfer.modem.oscillator_search->rf_shift_hz==0 &&
          model->inputs.fake_hopping.available &&
          model->inputs.fake_hopping.experimental && model->inputs.fake_hopping.spacing_hz<100000 &&
          model->inputs.options.modem.oscillator_search->rf_shift_hz==
              model->inputs.fake_hopping.last_hz-controller.settings().transfer.modem.carrier_hz &&
          model->inputs.options.modem.oscillator_search->rf.accuracy_ppm==100,
          "fake hopping must plan the highest shift with the RF model without tuning the actual stream");
    for(const auto* unsupported:{"genuine","ic-7100"}) {
        controller.select(F::fhss,unsupported);
        check(controller.field(F::fhss).selected=="fake-fcc","disabled hardware hopping became selectable");
    }
    controller.select(F::key,"none");
    load(controller,"--dsss-mode interleave");load(controller,"--dsss-version legacy");
    const auto exported=controller.field(F::planner_command).text;
    load(controller,exported);
    check(controller.settings().transfer.audio_timing_error_seconds==.012345,
        "audio allowance lost its saved parameter-list value");
    check(controller.settings().transfer.clock_sync==accepted && controller.field(F::dsss_factor).selected=="100" &&
          controller.field(F::fhss).selected=="fake-fcc"&&controller.field(F::dsss_version).selected=="legacy",
          "clock/spread/version settings lost their loaded values");
    check(controller.field(F::status).text.find("No clock/RAM fit")==std::string::npos,
        "narrow experimental hopping retained the obsolete 19.9 MHz search expansion");
    controller.select(F::fhss,"off");
    load(controller,"--clock-sync default");
    check(!controller.settings().transfer.clock_sync,
        "supported partial clock import did not commit Default timing");
    check(controller.settings().transfer.audio_timing_error_seconds==.012345,
        "loading an older command without audio error changed its retained value");
    check(controller.field(F::dsss_version).selected=="legacy",
        "partial parameter-list load silently replaced explicit legacy DSSS");
    load(controller,"--dsss-version interleaved-v2");
    if(controller.field(F::dsss_version).selected!="interleave"||
       controller.field(F::dsss_factor).selected!="100"||controller.settings().transfer.modem.bandwidth_hz!=36)
        std::cerr<<"version load: "<<controller.field(F::dsss_version).selected<<" / "
            <<controller.field(F::dsss_factor).selected<<" / "<<controller.settings().transfer.modem.bandwidth_hz
            <<" / "<<controller.field(F::status).text<<'\n';
    check(controller.field(F::dsss_version).selected=="interleave"&&
        controller.field(F::dsss_factor).selected=="100"&&controller.settings().transfer.modem.bandwidth_hz==36,
        "version-only parameter-list load changed factor or Rate");
    controller.close();
}
void arithmetic_settings() {
    using A=modem::SearchArithmetic;
    Controller controller;
    check(controller.field(F::search_arithmetic).selected=="default"&&
        controller.settings().transfer.modem.search_arithmetic==A::default_mode,
        "new application must retain the distinct default arithmetic request");
    controller.edit(F::short_bits,"001");
    const auto base=controller.settings().transfer.modem;
    for(const auto* id:{"fp32-min","fp64-force","default"}) {
        controller.select(F::search_arithmetic,id);
        const auto arithmetic=tuning::parse_search_arithmetic(id);
        const auto& settings=controller.settings();
        check(controller.field(F::search_arithmetic).selected==id&&
            settings.transfer.modem.search_arithmetic==arithmetic&&settings.long_message_modem&&
            settings.long_message_modem->search_arithmetic==arithmetic&&
            settings.transfer.modem.sample_rate==base.sample_rate&&
            settings.transfer.modem.carrier_hz==base.carrier_hz&&
            modem::symbol_sample_count(settings.transfer.modem)==modem::symbol_sample_count(base)&&
            controller.field(F::short_bits).text=="001",
            "arithmetic selection failed to reach both runtime profiles or changed waveform/draft geometry");
        const auto saved=launch_command::parse(controller.field(F::planner_command).text);
        check(saved.search_arithmetic==id,"launch export merged distinct arithmetic aliases");
        Controller restored({false,false,saved});
        check(restored.settings().transfer.modem.search_arithmetic==arithmetic&&
            restored.field(F::search_arithmetic).selected==id,
            "saved arithmetic selection failed to restore at startup");
        restored.close();
    }
    load(controller,"--search-arithmetic fp64");
    load(controller,"--tx-dbm 20");
    check(controller.field(F::search_arithmetic).selected=="fp64-force"&&
        controller.settings().transfer.modem.search_arithmetic==A::fp64,
        "older partial settings reset the omitted arithmetic request");
    const auto accepted_rate=controller.settings().transfer.modem.bandwidth_hz;
    load(controller,"--rate 100 --search-arithmetic matrix4");
    check(controller.field(F::search_arithmetic).selected=="fp64-force"&&
        controller.settings().transfer.modem.bandwidth_hz==accepted_rate&&
        controller.field(F::status).text.find("not implemented")!=std::string::npos,
        "unavailable 4-bit import partially changed active settings or lacked an explanation");
    load(controller,"--rate 100 --search-arithmetic unknown");
    check(controller.field(F::search_arithmetic).selected=="fp64-force"&&
        controller.settings().transfer.modem.bandwidth_hz==accepted_rate,
        "unknown arithmetic import partially applied another setting");
    for(const auto* legacy:{"int8","INT8","matrix8","8-bit"}) {
        load(controller,std::string("--search-arithmetic ")+legacy);
        check(controller.field(F::search_arithmetic).selected=="default"&&
            controller.settings().transfer.modem.search_arithmetic==A::default_mode&&
            launch_command::parse(controller.field(F::planner_command).text).search_arithmetic=="default",
            "legacy integer settings did not migrate atomically to the automatic policy");
    }
    load(controller,"--search-arithmetic default");
    check(controller.field(F::search_arithmetic).selected=="default"&&
        controller.settings().transfer.modem.search_arithmetic==A::default_mode,
        "explicit default failed to restore the saved default request");
    controller.close();
}
void scheduled_hop_settings() {
    Controller controller;
    const auto& burst=controller.field(F::sched_hop);
    check(burst.selected=="0"&&burst.options.size()==3&&
        burst.options[0].id=="0"&&burst.options[0].label=="0"&&
        burst.options[1].id=="3"&&burst.options[2].id=="5",
        "Scheduled burst defaults or exact dropdown choices changed");
    for(const auto field:{F::sched_hop,F::sched_frequency_search}) {
        const auto& declarations=ui::console_screen();
        const auto control=std::find_if(declarations.begin(),declarations.end(),[&](const auto& c){return c.field==field;});
        check(control!=declarations.end()&&control->kind==ui::Kind::choice&&control->persistent&&!control->developer_only,
            "Burst controls must be ordinary persistent dropdowns");
    }
    const auto& declarations=ui::console_screen();
    const auto dilution_control=std::find_if(declarations.begin(),declarations.end(),[](const auto& c){return c.field==F::sched_hop_dilution;});
    check(dilution_control!=declarations.end()&&dilution_control->kind==ui::Kind::choice&&
        dilution_control->document_only&&dilution_control->developer_only&&dilution_control->page==ui::Page::planner,
        "Burst dilution must stay a persistent developer-only planner control");
    check(controller.field(F::sched_hop_dilution).selected=="0"&&!controller.field(F::sched_hop_dilution).enabled&&
        controller.field(F::sched_hop_dilution).options.size()==2&&
        controller.field(F::sched_hop_dilution).options[0].id=="0"&&controller.field(F::sched_hop_dilution).options[1].id=="3",
        "Burst dilution must default to one inactive hop group");
    check(!burst.enabled&&!controller.field(F::sched_frequency_search).enabled&&
        !controller.settings().transfer.modem.scheduled_hopping&&
        controller.settings().transfer.modem.frequency_search_multiplier==1,
        "Unencrypted startup must not enable a keyed burst schedule");
    load(controller,"--sched-hop 5 --sched-frequency-search 8");
    check(controller.field(F::sched_hop).selected=="5"&&
        controller.field(F::sched_frequency_search).selected=="8"&&
        !controller.settings().transfer.modem.scheduled_hopping&&
        controller.settings().transfer.modem.frequency_search_multiplier==1&&
        !controller.field(F::sched_hop).enabled&&!controller.field(F::sched_frequency_search).enabled,
        "Unkeyed import must retain preference without activating secret scheduling or expanded search");
    load_synthetic_key(controller);
    load(controller,"--pattern pattern-16 --rate 1200 --carrier 1500 --sched-hop 5 --sched-frequency-search 4 --sched-hop-dilution 3");
    check(controller.field(F::pattern).selected=="pattern-16"&&
        controller.settings().transfer.modem.spreading_factor==16&&
        controller.settings().long_message_modem&&controller.settings().long_message_modem->spreading_factor==16,
        "Scheduled control fixture did not load its supported fixed short/long pattern");
    for(const auto* id:{"3","5"}) {
        controller.select(F::sched_hop,id);
        const auto& config=controller.settings().transfer.modem;
        if(!config.scheduled_hopping)throw Error("Scheduled import was not applied: "+controller.field(F::status).text);
        check(config.scheduled_hopping->time_multiplier==modem::sched_hop_multiplier(id)&&
            config.frequency_search_multiplier==4&&config.scheduled_hopping->group_hops==3&&
            controller.settings().long_message_modem&&
            controller.settings().long_message_modem->scheduled_hopping&&
            controller.settings().long_message_modem->scheduled_hopping->time_multiplier==modem::sched_hop_multiplier(id)&&
            controller.settings().long_message_modem->frequency_search_multiplier==4&&
            controller.settings().long_message_modem->scheduled_hopping->group_hops==3,
            "Short/long modem profiles lost scheduled burst or frequency-search policy");
        check(config.scheduled_hopping->clock_error_seconds==.01&&config.scheduled_hopping->peer_clock_error_seconds==.01,
            "Default scheduled plan lost per-endpoint clock bounds");
        check(config.scheduled_hopping->region_seconds==0&&config.scheduled_hopping->offset_seconds==0,
            "Default scheduled plan lost propagation region/offset bounds");
        check(config.scheduled_hopping->audio_error_seconds==clock_sync::default_audio_error_seconds,
            "Default scheduled plan lost the audio-lag bound");
        const auto& longer=*controller.settings().long_message_modem;
        check(config.data_key&&longer.data_key,
            "Scheduled short/long geometry did not bind its actual immutable key");
        const auto schedule_seed=controller.settings().transfer.key->stream(StreamPurpose::Fhss,3,
            32ULL*modem::sched_hop_multiplier(id),32,StreamDomain::SchedHopKeyV1);
        check(config.data_key->stream(StreamPurpose::Fhss,3,32ULL*modem::sched_hop_multiplier(id),32,
                StreamDomain::SchedHopKeyV1)==schedule_seed&&
            longer.data_key->stream(StreamPurpose::Fhss,3,32ULL*modem::sched_hop_multiplier(id),32,
                StreamDomain::SchedHopKeyV1)==schedule_seed,
            "Scheduled geometry substituted a nominal key for the selected key");
        check(std::isfinite(modem::bit_rate(config))&&modem::bit_rate(config)>0&&
            modem::bit_rate(config)==modem::bit_rate(longer),
            "Scheduled inspection could not resolve the keyed short/long calendar");
        check(controller.field(F::sched_hop).enabled&&controller.field(F::sched_frequency_search).enabled&&
            controller.field(F::sched_hop_dilution).enabled,
            "Selected encrypted burst mode left frequency expansion disabled");
        const auto saved=launch_command::parse(controller.field(F::planner_command).text);
        check(saved.sched_hop==modem::sched_hop_multiplier(id)&&saved.sched_frequency_search==4&&saved.sched_hop_dilution==3,
            "Burst selections missing from exported launch/parameter-list command");
        load(controller,"--tx-dbm 20");
        check(controller.field(F::sched_hop).selected==id&&
            controller.settings().transfer.modem.frequency_search_multiplier==4,
            "Older partial parameter list reset burst preferences");
    }
    load(controller,"--sched-hop 0 --rate 1200 --carrier 1500");
    const auto ungated_rate=controller.settings().transfer.modem.sample_rate;
    load(controller,"--sched-hop 5 --sched-hop-dilution 0");
    check(controller.settings().transfer.modem.sample_rate==ungated_rate,
        "Gate allowance needlessly discarded an already sufficient aligned sample rate");
    load(controller,"--rate 0.1 --carrier 0.5");
    const auto& narrow=controller.settings().transfer.modem;
    const auto gate=modem::waveform_gate_bandwidth_allowance_hz(narrow);
    const auto upper=narrow.carrier_hz+(1+modem::pattern_pulse_rolloff)*modem::waveform_bandwidth_hz(narrow)/2+gate/2;
    check(narrow.bandwidth_hz==.1&&narrow.carrier_hz==.5&&gate==200&&upper<=.45*narrow.sample_rate,
        "Narrow burst sampling omitted the gate allowance or changed its chip/carrier settings");
    const auto extent=controller.spectrum_search_band();
    check(extent&&extent->second>=narrow.carrier_hz+gate/2,
        "Searched-spectrum inspection omitted scheduled gate sidebands");
    load(controller,"--rate 1200 --carrier 1500 --sched-hop 5 --sched-hop-dilution 3 --clock-sync GPS_1ms-10ms_region-2564ms_offset --audio-error 60ms");
    const auto& custom=controller.field(F::sched_hop_dilution);
    check(custom.selected=="3"&&custom.options.size()==2,
        "Imported dilution added obsolete dropdown choices");
    const auto custom_plan=controller.settings().transfer.modem.scheduled_hopping;
    check(custom_plan&&custom_plan->group_hops==3&&custom_plan->clock_error_seconds==.001&&
        custom_plan->peer_clock_error_seconds==.001&&custom_plan->region_seconds==.01&&
        custom_plan->offset_seconds==2.564&&custom_plan->audio_error_seconds==.06,
        "Scheduled guard geometry did not retain selected clock/propagation/audio allowances");
    controller.select(F::sched_hop,"3");
    check(controller.field(F::sched_hop_dilution).selected=="3"&&
        controller.settings().transfer.modem.scheduled_hopping->continuous()&&
        !modem::scheduled_bursts(controller.settings().transfer.modem),
        "All-active limit retained split-symbol search or changed dilution");
    load(controller,"--sched-hop-dilution 3 --clock-sync default --audio-error default");
    controller.select(F::sched_hop,"0");
    check(!controller.settings().transfer.modem.scheduled_hopping&&
        controller.settings().transfer.modem.frequency_search_multiplier==1&&
        !controller.field(F::sched_frequency_search).enabled&&!controller.field(F::sched_hop_dilution).enabled&&
        controller.field(F::sched_frequency_search).selected=="4"&&controller.field(F::sched_hop_dilution).selected=="3",
        "Disabling burst lost inactive frequency/dilution preferences");
    controller.select(F::sched_frequency_search,"32");
    check(controller.field(F::sched_frequency_search).selected=="4",
        "Disabled frequency control accepted an edit");
    controller.select(F::sched_hop,"5");
    check(controller.settings().transfer.modem.frequency_search_multiplier==4,
        "Re-enabling burst lost remembered frequency expansion");
    const auto accepted=controller.settings().transfer.modem;
    load(controller,"--rate 360 --sched-hop 10");
    check(controller.settings().transfer.modem.bandwidth_hz==accepted.bandwidth_hz&&
        controller.field(F::sched_hop).selected=="5",
        "Invalid burst import partially applied another setting");
    load(controller,"--rate 360 --sched-hop-dilution 20");
    check(controller.settings().transfer.modem.bandwidth_hz==accepted.bandwidth_hz&&
        controller.field(F::sched_hop_dilution).selected=="3",
        "Dilution outside the current frame partially applied another setting");
    controller.select(F::key,"none");
    check(!controller.settings().transfer.modem.scheduled_hopping&&
        !controller.field(F::sched_hop).enabled&&!controller.field(F::sched_frequency_search).enabled&&
        !controller.field(F::sched_hop_dilution).enabled,
        "Removing the transmit key retained active burst scheduling");
    controller.close();
    check(!controller.field(F::sched_hop).enabled&&!controller.field(F::sched_frequency_search).enabled&&
        !controller.field(F::sched_hop_dilution).enabled,
        "Closed controller retained editable burst controls");
}
void receiver_strategy_settings() {
    using S=modem::ReceiverStrategy;
    Controller controller;
    check(controller.settings().transfer.modem.receiver_strategy==S::automatic&&
        controller.settings().transfer.modem.receiver_cpu_budget==.6,
        "older startup settings must use automatic strategy and the default budget");
    controller.edit(F::short_bits,"001");
    const auto base=controller.settings().transfer.modem;
    controller.select(F::search_arithmetic,"fp64-force");
    for(const auto* id:{"existing","complex","amplitude","auto"}) {
        load(controller,std::string("--receiver-strategy ")+id+" --receiver-cpu-budget 0.37");
        const auto expected=tuning::parse_receiver_strategy(id);const auto& settings=controller.settings();
        const auto context=std::string("receiver strategy import ")+id;
        if(settings.transfer.modem.receiver_strategy!=expected||settings.transfer.modem.receiver_cpu_budget!=.37||
           !settings.long_message_modem||settings.long_message_modem->receiver_strategy!=expected||
           settings.long_message_modem->receiver_cpu_budget!=.37)
            throw Error(context+" lost short/long policy propagation: "+controller.field(F::status).text);
        if(settings.transfer.modem.search_arithmetic!=modem::SearchArithmetic::fp64)
            throw Error(context+" changed the independent arithmetic request");
        if(settings.transfer.modem.sample_rate!=base.sample_rate||settings.transfer.modem.carrier_hz!=base.carrier_hz||
           modem::symbol_sample_count(settings.transfer.modem)!=modem::symbol_sample_count(base))
            throw Error(context+" retuned the transmitted waveform geometry");
        check(controller.field(F::short_bits).text=="001","receiver policy import changed exact raw draft bits");
        const auto saved=launch_command::parse(controller.field(F::planner_command).text);
        check(saved.receiver_strategy==id&&saved.receiver_cpu_budget==.37,
            "receiver strategy/budget missing from saved launch parameters");
        Controller restored({false,false,saved});
        check(restored.settings().transfer.modem.receiver_strategy==expected&&
            restored.settings().transfer.modem.receiver_cpu_budget==.37&&
            restored.field(F::receiver_strategy).selected==id,
            "receiver controls failed to restore at startup");restored.close();
        load(controller,"--tx-dbm 20");
        check(controller.settings().transfer.modem.receiver_strategy==expected&&
            controller.settings().transfer.modem.receiver_cpu_budget==.37,
            "older partial load reset the selected receiver policy");
    }
    const auto accepted=controller.settings().transfer.modem;
    load(controller,"--rate 100 --receiver-cpu-budget 0");
    check(controller.settings().transfer.modem.bandwidth_hz==accepted.bandwidth_hz&&
        controller.settings().transfer.modem.receiver_cpu_budget==accepted.receiver_cpu_budget,
        "invalid receiver budget partially applied another setting");
    load(controller,"--rate 100 --receiver-strategy unknown");
    check(controller.settings().transfer.modem.bandwidth_hz==accepted.bandwidth_hz,
        "invalid receiver strategy partially applied another setting");
    controller.close();
}
void doppler_settings() {
    Controller controller;
    check(controller.field(F::doppler).text=="0.000000c"&&controller.field(F::doppler).display_text=="0%",
        "manual Doppler must default to zero with a visible percentage");
    const auto& screen=ui::console_screen();
    const auto control=std::find_if(screen.begin(),screen.end(),[](const auto& c){return c.field==F::doppler;});
    check(control!=screen.end()&&control->kind==ui::Kind::text&&control->persistent&&!control->developer_only,
        "Doppler must remain an ordinary editable persistent control");
    load(controller,"--rate 1200 --carrier 1009000 --shift 1000000 --oscillator gpsdo-xo --doppler \"100 kph\"");
    const auto configured=controller.field(F::carrier).text;
    const auto expected=tuning::doppler_carrier_hz(1009000,tuning::parse_doppler("100 kph"))-1000000;
    near(controller.settings().transfer.modem.carrier_hz,expected,"Doppler failed to adjust carrier before subtracting Shift");
    near(controller.link_plan()->inputs.options.modem.carrier_hz,expected,"Planner used the unadjusted frequency");
    const auto saved=controller.field(F::planner_command).text;const auto patch=launch_command::parse(saved);
    check(patch.carrier_hz==1009000&&patch.rf_shift_hz==1000000&&patch.doppler,
        "Export compounded Doppler into the configured carrier");
    load(controller,saved);
    near(controller.settings().transfer.modem.carrier_hz,expected,"Doppler round trip compounded frequency adjustment");
    check(controller.field(F::carrier).text==configured,"Doppler changed configured Carrier display");
    controller.edit(F::doppler,"-100 mph");
    check(controller.settings().transfer.modem.carrier_hz>9000&&controller.field(F::doppler).display_text.find('%')!=std::string::npos,
        "Approaching manual velocity did not raise carrier or show percentage");
    const auto accepted=controller.settings().transfer.modem.carrier_hz;
    load(controller,"--rate 100 --doppler 2c");
    near(controller.settings().transfer.modem.carrier_hz,accepted,"Invalid Doppler import partially applied settings");
    near(controller.settings().transfer.modem.bandwidth_hz,1200,"Invalid Doppler changed Rate");
    load(controller,"--carrier 1009500 --shift 1000000");
    near(controller.settings().transfer.modem.carrier_hz,
        tuning::doppler_carrier_hz(1009500,tuning::parse_doppler("-100 mph"))-1000000,
        "partial frequency import lost the retained Doppler correction");
    const auto accepted_command=controller.field(F::planner_command).text;
    const auto accepted_carrier=controller.field(F::carrier).text;
    load(controller,"--carrier 1000000 --shift 1000000 --doppler 100kph");
    check(controller.field(F::carrier).text==accepted_carrier&&controller.field(F::planner_command).text!=accepted_command&&
        controller.field(F::doppler).text==tuning::parse_doppler("-100 mph").canonical,
        "invalid complete adjusted stream partially committed field state");
    controller.edit(F::doppler,"0.000000c");
    near(controller.settings().transfer.modem.carrier_hz,9500,"Zero Doppler failed to restore configured carrier");
    controller.close();
}
void duplex_settings() {
    Controller controller({false,false});
    const auto& screen=ui::console_screen();
    const auto declaration=std::find_if(screen.begin(),screen.end(),[](const auto& control){return control.field==F::live_duplex;});
    check(declaration!=screen.end()&&declaration->kind==ui::Kind::toggle&&declaration->persistent&&
        !declaration->developer_only&&std::string_view(declaration->help).find("loopback")!=std::string_view::npos,
        "Live/Duplex requires an ordinary persistent checkbox with physical loopback help");
    check(!controller.settings().full_duplex&&!controller.field(F::live_duplex).checked&&controller.field(F::live_duplex).enabled,
        "hardware audio must preserve default half duplex");
    controller.toggle(F::live_duplex,true);
    check(controller.settings().full_duplex&&controller.field(F::live_duplex).checked&&
        launch_command::parse(controller.field(F::planner_command).text).full_duplex==true,
        "Live/Duplex toggle did not reach runtime and exported settings");
    load(controller,"--rate 1200");
    check(controller.settings().full_duplex&&controller.field(F::live_duplex).checked,
        "loading an older command reset an omitted duplex setting");
    load(controller,"--live-duplex no");
    check(!controller.settings().full_duplex&&!controller.field(F::live_duplex).checked,
        "explicit half duplex failed to restore default capture behavior");
    load(controller,"--live-duplex yes");
    controller.select(F::simulation,"yes");
    check(!controller.field(F::live_duplex).enabled&&controller.field(F::live_duplex).checked,
        "Simulation must disable the hardware-only toggle while remembering its selection");
    controller.toggle(F::live_duplex,false);
    check(controller.settings().full_duplex,"disabled Simulation control accepted a duplex callback");
    controller.select(F::simulation,"no");
    check(controller.field(F::live_duplex).enabled&&controller.settings().full_duplex,
        "returning to hardware lost its remembered duplex setting");
    controller.close();controller.toggle(F::live_duplex,false);
    check(!controller.field(F::live_duplex).enabled&&controller.settings().full_duplex,
        "closing controller accepted a duplex callback");
}
void fhss_profiles() {
    using P=fhss::Preference;
    auto config=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10).config;
    config.oscillator_search=modem::OscillatorSearchConfig{};
    const auto rf=[&](double carrier){config.oscillator_search->rf_shift_hz=carrier-config.carrier_hz;};
    const auto contains=[&](const fhss::Profile& p) {
        check(p.available&&p.channels>=2&&p.channels<=200,"FHSS profile unavailable or invalid count");
        check(p.first_hz>=config.carrier_hz,"illustrated hop requires negative Shift");
        check(p.first_hz-p.guarded_hz/2>=p.band.lower_hz-.000001&&
              p.last_hz+p.guarded_hz/2<=p.band.upper_hz+.000001,"guarded hop set escaped band");
        near(p.span_hz,(p.channels-1)*p.spacing_hz,"FHSS span/count mismatch");
        check(p.observer()->channels==p.channels&&p.observer()->dwell_seconds==p.dwell_seconds,
            "observer received a different channel count/dwell");
    };
    rf(30001500);
    for(const auto pref:{P::fcc,P::eu}) {
        const auto p=fhss::resolve(pref,config);contains(p);
        check(p.experimental&&p.channels==200&&p.spacing_hz<10000,"experimental FHSS retained wide spacing floor");
        near(p.spacing_hz,1.25*std::max(p.occupied_hz,modem::waveform_bandwidth_hz(config))+200,
            "experimental waveform and smooth-ramp filter margin incorrect");
        const auto limited=fhss::resolve(pref,config,fhss::Band{30e6,30.03e6});contains(limited);
        check(limited.channels<200,"narrow operating band failed to bound channel count");
        check(!fhss::resolve(pref,config,fhss::Band{30e6,30e6+10}).available,"impossible experimental band admitted");
    }
    const auto strong=tuning::resolve(15.17055,200,tuning::PatternMode::auto_keystream,true,10).config;
    const auto weak=tuning::resolve(15.17055,0,tuning::PatternMode::auto_keystream,true,10).config;
    check(!modem::pattern_pulse_enabled(strong)&&modem::pattern_pulse_enabled(weak),
        "fractional target fixture lost the rectangular-to-shaped transition");
    const auto strong_hop=fhss::resolve(P::fcc,strong),weak_hop=fhss::resolve(P::fcc,weak);
    near(strong_hop.spacing_hz,weak_hop.spacing_hz,"planner target changed conservative hop geometry");
    check(strong_hop.guarded_hz>=1.25*strong_hop.occupied_hz&&weak_hop.guarded_hz>=1.25*weak_hop.occupied_hz,
        "hop envelope did not retain both pulse eligibility alternatives");
    auto wide=config;wide.pulse_shaping=false;wide.dsss_factor=1;wide.oscillator_search->rf_shift_hz=915e6-wide.carrier_hz;
    wide.bandwidth_hz=200000;
    check(fhss::resolve(P::fcc,wide).channels==25,"FCC 250 kHz boundary did not select 25 channels");
    wide.bandwidth_hz=400001;
    check(!fhss::resolve(P::fcc,wide).available,"FCC width limit accepted oversized channel");
    rf(800e6-1);check(fhss::resolve(P::eu,config).experimental,"sub-800 convention boundary wrong");
    rf(800e6);check(!fhss::resolve(P::eu,config).available,"800MHz silently uses experimental profile");
    rf(915e6);auto p=fhss::resolve(P::fcc,config);contains(p);check(p.channels==50&&p.spacing_hz==25000,"FCC902 narrow profile wrong");
    check(!fhss::resolve(P::eu,config).available,"EU915 borrowed FCC profile");
    rf(866e6);p=fhss::resolve(P::eu,config);contains(p);
    check(p.channels==47&&p.spacing_hz==100000&&p.last_hz+p.guarded_hz/2<=868.6e6&&
        p.assumptions.find("0.1%")!=std::string::npos,"EU subGHz profile/exclusions/duty missing");
    rf(868.65e6);check(!fhss::resolve(P::eu,config).available,"alarm band borrowed 2.4GHz profile");
    rf(2440e6);p=fhss::resolve(P::fcc,config);contains(p);check(p.channels==75,"FCC2.4 profile wrong");
    p=fhss::resolve(P::eu,config);contains(p);
    check(p.channels==79&&p.occupancy_window_seconds==6&&p.span_hz>=.7*83.5e6&&p.assumptions.find("CCA/DAA")!=std::string::npos,"EU2.4 coverage/access missing");
    check(!fhss::resolve(P::eu,config,fhss::Band{2400e6,2420e6}).available,"EU coverage silently shrunk to selected band");
    rf(5800e6);p=fhss::resolve(P::fcc,config);contains(p);check(p.channels==75,"FCC5.8 profile wrong");
    p=fhss::resolve(P::eu,config);contains(p);check(p.channels==20&&p.span_hz>135e6,"EU5.8 coverage wrong");
    rf(5870e6);contains(fhss::resolve(P::eu,config));check(!fhss::resolve(P::fcc,config).available,"FCC band extended to EU upper limit");
    rf(928e6-1);contains(fhss::resolve(P::fcc,config));
    rf(433e6);const auto narrow=fhss::resolve(P::fcc,config);
    config.bandwidth_hz=.01;config.spreading_factor=64;config.dsss_factor=1;
    const auto tiny=fhss::resolve(P::fcc,config);contains(tiny);
    check(tiny.spacing_hz-200<1&&
          tiny.span_hz-200*(tiny.channels-1)<(narrow.span_hz-200*(narrow.channels-1))/1000,
          "very narrow waveform kept a fixed floor beyond the explicit 200 Hz ramp allowance");
    Controller controller({false,true});
    load_synthetic_key(controller);
    const auto& choices=controller.field(F::fhss).options;
    check(choices.size()==3&&choices[0].label=="Off"&&choices[1].label=="Fake: FCC"&&choices[2].label=="Fake: EU",
        "FHSS must have exactly the three requested choices");
    load(controller,"--oscillator gpsdo-ocxo --rf-oscillator gpsdo-ocxo --shift 30MHz --carrier 30.0015MHz --fhss fake-eu");
    check(controller.field(F::fhss).selected=="fake-eu"&&controller.link_plan()->inputs.fake_hopping.experimental,
        "EU preference lost below 800MHz");
    load(controller,"--shift 2.44GHz --carrier 2.4400015GHz");
    check(controller.field(F::fhss).selected=="fake-eu"&&controller.link_plan()->inputs.fake_hopping.channels==79,
        "RF change lost EU preference or profile");
    load(controller,"--shift 30MHz --carrier 30.0015MHz");
    check(controller.field(F::fhss).selected=="fake-eu"&&controller.link_plan()->inputs.fake_hopping.experimental,
        "returning to low RF lost EU preference");
    load(controller,"--shift 1GHz --carrier 1.0000015GHz");
    check(controller.field(F::fhss).selected=="fake-eu"&&!controller.link_plan()->inputs.fake_hopping.available&&
        !controller.link_plan()->inputs.observer_hopping,"unsupported band retained a hopping estimate");
    // Doppler-adjusted RF, not the unchanged carrier editor, chooses the band.
    load(controller,"--shift 799.99MHz --carrier 799.999MHz --doppler -0.00001c");
    check(!controller.link_plan()->inputs.fake_hopping.experimental,"manual Doppler ignored in actual RF boundary");
    controller.close();
}
void fhss_list_settings() {
    const auto list=fhss::parse_list("# Shift frequencies, irregular\n30 MHz;30.006MHz\n30020000Hz # last\n");
    check(list==std::vector<double>({30000000,30006000,30020000}),"FHSS units/order/comments incorrect");
    check(fhss::parse_list(fhss::list_text(list))==list,"FHSS frequency serialization lost precision");
    for(const auto text:{"1 MHz","1MHz,1000000Hz","NaN,2MHz","-1,2MHz","1MHz,inf"}) {
        bool rejected=false;try{(void)fhss::parse_list(text);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid FHSS list accepted");
    }
    std::vector<double> full(200);for(unsigned i=0;i<200;++i)full[i]=1e6+i*1e4;
    check(fhss::parse_list(fhss::list_text(full))==full,"Full bounded list failed");
    bool rejected=false;try{(void)fhss::parse_list(fhss::list_text(full)+",4MHz");}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Oversized list accepted");
    check(fhss::parse_list("0,6000")==std::vector<double>({0,6000}),"Zero Shift rejected");
    const std::vector<double> rf_list{30001500,30007500,30021500};
    Controller controller({false,true});controller.set_developer_mode(true);load_synthetic_key(controller);
    load(controller,"--oscillator gpsdo-xo --rf-oscillator gpsdo-ocxo --shift 30MHz --carrier 30.0015MHz --rate 360 --fhss fake-list --fhss-list '30MHz,30.006MHz,30.02MHz'");
    const auto profile=controller.link_plan()->inputs.fake_hopping;
    check(profile.available&&profile.centers==rf_list&&profile.channels==3&&profile.spacing_hz==6000&&profile.span_hz==20000&&profile.last_hz==30021500,
        "Irregular list geometry not used by planner");
    check(profile.observer()&&profile.center(1)==30007500,"Indexed irregular center or observer model incorrect");
    const auto zero=fhss::resolve(fhss::Preference::list,controller.settings().transfer.modem,std::nullopt,std::vector<double>{0,6000});
    check(zero.available&&zero.first_hz==1500&&zero.last_hz==7500,"Shift list failed to add unchanged audio carrier");
    auto adjusted=controller.settings().transfer.modem;adjusted.carrier_hz+=7;
    const auto shifted=fhss::resolve(fhss::Preference::list,adjusted,std::nullopt,list);
    check(shifted.last_hz==profile.last_hz+7,"List RF geometry ignored adjusted audio carrier");
    const auto& input=controller.link_plan()->inputs;
    near(input.options.modem.carrier_hz+input.options.modem.oscillator_search->rf_shift_hz,profile.last_hz,"Planner did not use highest listed RF");
    check(!fhss::resolve(fhss::Preference::list,controller.settings().transfer.modem,fhss::Band{30e6,30.01e6},list).available,"Out-of-band custom list admitted");
    const std::vector<double> overlap{30000000,30000001};
    const auto overlapping=fhss::resolve(fhss::Preference::list,controller.settings().transfer.modem,std::nullopt,overlap);
    check(overlapping.available&&!overlapping.observer(),"Overlapping custom channels received disjoint observer model");
    const auto before=launch_command::parse(controller.field(F::planner_command).text);
    controller.edit(F::fhss_list,"1MHz");controller.activate(C::fhss_apply);
    check(controller.link_plan()->inputs.fake_hopping.centers==rf_list&&controller.field(F::fhss_list_status).text.find("requires")!=std::string::npos,
        "Invalid editor list changed applied profile or lacked error");
    controller.edit(F::fhss_list,"30.005MHz\n30.03MHz");controller.activate(C::fhss_apply);
    auto saved=launch_command::parse(controller.field(F::planner_command).text);
    check(saved.fhss_list=="30005000,30030000"&&saved.fhss=="fake-list","Applied list missing from launch export");
    controller.select(F::fhss,"off");load(controller,"--path-loss-db 100");
    check(launch_command::parse(controller.field(F::planner_command).text).fhss_list==saved.fhss_list,"Partial load or Off discarded list");
    load(controller,launch_command::format(before));
    const auto dir=std::filesystem::temp_directory_path()/"dp-fhss-list-20261011";
    std::filesystem::create_directories(dir);const auto path=dir/"frequencies with spaces.txt";
    {std::ofstream out(path);out<<"30 MHz\n30.006 MHz\n30.02 MHz\n";}
    const auto file_patch=launch_command::parse("--oscillator gpsdo-xo --rf-oscillator gpsdo-ocxo --shift 30MHz --carrier 30.0015MHz --fhss fake-list --fhss-list-file '"+path.string()+"'");
    Controller startup({false,true,file_patch});
    check(!startup.link_plan()->inputs.fake_hopping.available&&!startup.link_plan()->inputs.observer_hopping&&
        startup.field(F::fhss).display_text.find("key required")!=std::string::npos,"Unkeyed CLI enabled FHSS");
    check(launch_command::parse(startup.field(F::planner_command).text).fhss_list==fhss::list_text(list),"File-only unkeyed startup discarded saved list");
    load_synthetic_key(startup);
    check(startup.link_plan()->inputs.fake_hopping.centers==rf_list,"File-only startup did not load frequencies");
    const auto file_export=launch_command::parse(startup.field(F::planner_command).text);
    check(file_export.fhss_list==fhss::list_text(list)&&!file_export.fhss_list_file,"Export retained mutable source file dependency");
    {std::ofstream out(path);out<<"bad";}
    load(controller,launch_command::format(file_patch));
    check(controller.link_plan()->inputs.fake_hopping.centers==rf_list,"Bad file partially applied settings");
    {std::ofstream out(path);out<<std::string(8193,'0');}
    load(controller,launch_command::format(file_patch));
    check(controller.field(F::status).text.find("8192")!=std::string::npos&&controller.link_plan()->inputs.fake_hopping.centers==rf_list,"Oversized file not rejected atomically");
    std::filesystem::remove(path);startup.close();
    controller.select(F::key,"none");
    check(!controller.field(F::fhss).enabled&&!controller.link_plan()->inputs.fake_hopping.available&&
        !controller.link_plan()->inputs.observer_hopping&&!controller.settings().transfer.modem.dsss,
        "Removing encryption key left spreading active");
    controller.close();
}
int main(int argc,char** argv) {
    try {exact_scheduled_target_import();if(argc>1&&std::string_view(argv[1])=="--sched-target-import"){std::cout<<"Exact scheduled target import passed\n";return 0;}scheduled_hop_settings();if(argc>1&&std::string_view(argv[1])=="--sched-hop"){std::cout<<"Scheduled hop settings passed\n";return 0;}receiver_strategy_settings();if(argc>1&&std::string_view(argv[1])=="--receiver-strategy"){std::cout<<"Receiver strategy settings passed\n";return 0;}fhss_list_settings();if(argc>1&&std::string_view(argv[1])=="--fhss-list"){std::cout<<"FHSS list passed\n";return 0;}fhss_profiles();if(argc>1&&std::string_view(argv[1])=="--fhss-profiles"){std::cout<<"FHSS profiles passed\n";return 0;}doppler_settings();arithmetic_settings();duplex_settings();clock_and_spread_controls();generated_and_pasted_settings();rejected_settings_remain_atomic();startup_and_submit_behavior();narrow_rate_round_trips();live_validation_is_atomic();real_radio_configuration();invalid_shift_edit_recovers();absolute_carrier_and_shift();zero_shift_legacy_and_shared_round_trip();
        std::cout<<"Planner launch setting round trips passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
