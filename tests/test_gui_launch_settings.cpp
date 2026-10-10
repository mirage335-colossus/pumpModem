#include "application.hpp"
#include "controller.hpp"
#include "launch_command.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
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
    check(controller.field(F::audio_error).text=="0ms" && controller.settings().transfer.audio_timing_error_seconds==0&&
        std::any_of(controller.field(F::audio_error).options.begin(),controller.field(F::audio_error).options.end(),
            [](const auto& option){return option.id=="0ms";}),
        "audio timing needs its independently editable zero starting allowance");
    controller.edit(F::clock_accuracy,"0.1ms");
    check(controller.field(F::clock_region).text=="1ms"&&controller.field(F::clock_offset).text=="0ms"&&
        controller.settings().transfer.clock_sync->region_seconds==.001&&
        controller.settings().transfer.clock_sync->offset_seconds==0&&
        controller.settings().transfer.audio_timing_error_seconds==0,
        "initial custom clock selection must start at a one-ms region, zero offset and zero additional audio allowance");
    controller.edit(F::clock_accuracy,"Default");
    controller.edit(F::audio_error,"12345us");
    check(controller.settings().transfer.audio_timing_error_seconds==.012345 &&
        !controller.settings().transfer.clock_sync && controller.settings().transfer.modem.oscillator_search==oscillator,
        "editing audio error changed GPS/oscillator mode or lost precision");
    for(const auto reset:{F::clock_accuracy,F::clock_region,F::clock_offset}) {
        controller.edit(F::clock_accuracy,"GPS_0.1ms-400ms_region-2564ms_offset");
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
              "Default in any editor must reset the complete clock triplet");
        check(controller.settings().transfer.audio_timing_error_seconds==.012345,
            "Default clock reset erased independent audio allowance");
    }
    controller.edit(F::clock_accuracy,"100ns");controller.edit(F::clock_region,"1us");controller.edit(F::clock_offset,"0ms");
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
    controller.select(F::dsss_version,"interleave");
    near(controller.settings().transfer.modem.bandwidth_hz,360,"direct Interleave activation did not install its usable voice pair");
    controller.select(F::dsss_factor,"100");
    check(controller.field(F::dsss_factor).selected=="100" && controller.settings().transfer.modem.dsss_factor==1 &&
          controller.field(F::dsss_factor).display_text.find("key needed")!=std::string::npos,
          "outer DSSS must remain inactive and labeled when no private key is selected");
    near(controller.settings().transfer.modem.bandwidth_hz,36,"DSSS 100x did not install its voice-passband Rate");
    near(controller.settings().transfer.modem.carrier_hz,1500,"DSSS default did not retain a 1.5 kHz stream carrier");
    check(controller.field(F::dsss_version).selected=="interleave"&&
        controller.settings().transfer.modem.outer_dsss_version==datapump::modem::OuterDsssVersion::legacy_v1,
        "new DSSS selection must default to v2 without changing the unkeyed Off waveform");
    controller.select(F::dsss_version,"off");
    const auto off_saved=launch_command::parse(controller.field(F::planner_command).text);
    check(off_saved.dsss_mode=="off"&&off_saved.dsss_factor==100,"Off export lost remembered spreading");
    Controller off_copy({false,false,off_saved});
    check(off_copy.field(F::dsss_version).selected=="off"&&off_copy.field(F::dsss_factor).selected=="100"&&
        off_copy.settings().transfer.modem.dsss_factor==1,"saved Off mode activated spreading or lost its factor");off_copy.close();
    controller.select(F::dsss_version,"interleave");
    load(controller,"--dsss-version legacy");
    near(controller.settings().transfer.modem.bandwidth_hz,36,"version-only selection reset the entered Rate");
    near(controller.settings().transfer.modem.carrier_hz,1500,"version-only selection reset Carrier");
    check(controller.field(F::dsss_factor).selected=="100"&&
        launch_command::parse(controller.field(F::planner_command).text).dsss_version=="legacy",
        "legacy diagnostic choice changed DSSS factor or was omitted from export");
    controller.select(F::fhss,"fake-0.4s-200");
    const auto model=controller.link_plan();
    check(controller.settings().transfer.modem.oscillator_search->rf_shift_hz==0 &&
          model->inputs.options.modem.oscillator_search->rf_shift_hz==19900000 &&
          model->inputs.options.modem.oscillator_search->rf.accuracy_ppm==100,
          "fake hopping must plan the highest shift with the RF model without tuning the actual stream");
    for(const auto* unsupported:{"genuine","ic-7100"}) {
        controller.select(F::fhss,unsupported);
        check(controller.field(F::fhss).selected=="fake-0.4s-200","disabled hardware hopping became selectable");
    }
    const auto exported=controller.field(F::planner_command).text;
    load(controller,exported);
    check(controller.settings().transfer.audio_timing_error_seconds==.012345,
        "audio allowance lost its saved parameter-list value");
    check(controller.settings().transfer.clock_sync==accepted && controller.field(F::dsss_factor).selected=="100" &&
          controller.field(F::fhss).selected=="fake-0.4s-200"&&controller.field(F::dsss_version).selected=="legacy",
          "clock/spread/version settings lost their loaded values");
    // The saved Fake/crystal combination deliberately exceeds sampled RF
    // search headroom. Keep that rejection transactional, then use supported
    // geometry for the independent partial-import preservation assertions.
    check(controller.field(F::status).text.find("No clock/RAM fit")!=std::string::npos,
        "unsupported illustrated RF search must not be admitted on import");
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
int main() {
    try {doppler_settings();arithmetic_settings();duplex_settings();clock_and_spread_controls();generated_and_pasted_settings();rejected_settings_remain_atomic();startup_and_submit_behavior();narrow_rate_round_trips();live_validation_is_atomic();real_radio_configuration();invalid_shift_edit_recovers();absolute_carrier_and_shift();zero_shift_legacy_and_shared_round_trip();
        std::cout<<"Planner launch setting round trips passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
