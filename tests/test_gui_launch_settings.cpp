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
        initial.oscillator&&initial.rate_hz&&initial.carrier_hz&&initial.workspace_percent,
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
int main() {
    try {generated_and_pasted_settings();rejected_settings_remain_atomic();startup_and_submit_behavior();live_validation_is_atomic();
        std::cout<<"Planner launch setting round trips passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
