#include "application.hpp"
#include "launch_command.hpp"
#include "datapump/tuning.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const std::string& message) {
    try {function();}catch(const std::exception& error) {check(*error.what(),"errors must explain the invalid input");return;}
    throw std::runtime_error(message);
}
void parsing_and_formatting() {
    const auto settings=launch_command::parse(
        "./datapump-gui --auto-pattern --tx-dbm 3 --path-loss-db=170 --noise-dbm-hz -164 "
        "--oscillator gpsdo-ocxo --target-snr -8 --rate 3600 --carrier 1500 --dsp-workspace 50%");
    check(settings.tx_dbm==3&&settings.path_loss_db==170&&settings.noise_dbm_hz==-164&&
          settings.target_db_hz==-8&&settings.rate_hz==3600&&settings.carrier_hz==1500&&
          settings.workspace_percent==50&&settings.oscillator=="gpsdo-ocxo"&&settings.pattern=="auto-pattern",
          "complete command did not parse its concrete settings");
    const auto command=launch_command::format(settings);
    check(launch_command::parse(command)==settings,"canonical command must round-trip every setting exactly");
    check(command.find("--auto-pattern")!=std::string::npos&&command.find("--target-snr -8")!=std::string::npos&&
          command.find("--rate 3600")!=std::string::npos,"canonical command must use concise readable flags and numbers");
    auto precise=settings;precise.target_db_hz=std::nextafter(-47.,-48.);
    precise.short_target_db_hz=-20.1234567890123;precise.long_target_db_hz=23;
    check(launch_command::parse(launch_command::format(precise))==precise,
          "formatting must not change sample-sensitive target precision or individual overrides");
    for(const auto mode:datapump::tuning::pattern_modes()) {
        auto pattern=settings;pattern.pattern=datapump::tuning::pattern_mode_name(mode);
        check(launch_command::parse(launch_command::format(pattern))==pattern,"every actual pattern mode must retain its identity");
    }
    for(const auto& oscillator:datapump::tuning::oscillator_presets()) {
        auto model=settings;model.oscillator=oscillator.id;
        check(launch_command::parse(launch_command::format(model))==model,"every oscillator preset must round-trip");
    }
    const auto windows=launch_command::parse(R"("C:\Program Files\Data Pump\datapump-gui.exe" --rate "3.6 kHz"
        --carrier=1.5kHz --oscillator "GPSDO: OCXO" --target-snr=+23)");
    check(windows.rate_hz==3600&&windows.carrier_hz==1500&&windows.target_db_hz==23,
          "quoted Windows paths, frequency units, newlines and equals syntax must remain ordinary arguments");
    const auto portable=launch_command::parse(R"('.\datapump-gui.exe' --bw 1MHz --carrier 3e6 --tx-dbm -15)");
    check(portable.rate_hz==1000000&&portable.carrier_hz==3000000&&portable.tx_dbm==-15,
          "Windows separators and Hz unit aliases must survive tokenization");
    const auto duplicate=launch_command::parse("--rate 1200 --bw 3600 --tx-dbm 1 --tx-dbm=3 --short-target-snr -9 --target-snr -8");
    check(duplicate.rate_hz==3600&&duplicate.tx_dbm==3&&duplicate.short_target_db_hz==-9&&duplicate.target_db_hz==-8,
          "last duplicate wins while explicit short/long overrides remain independent of common-target order");
    const std::vector<std::string> tokens{"--oscillator","GPSDO: OCXO","--rate","3.6 kHz"};
    const auto exact=launch_command::parse_arguments(tokens);
    check(exact.oscillator=="gpsdo-ocxo"&&exact.rate_hz==3600,"startup tokens must be used without another shell parse");
}
void invalid_commands() {
    for(const auto* command:{"", "  \n\t", "./datapump-gui", "\"\" --tx-dbm 3", "--unknown 1", "--tx-dbm", "--tx-dbm=",
            "--tx-dbm --path-loss-db 170", "--rate 3600 trailing", "--rate '3600", "--auto-pattern=yes",
            "--target-snr nan", "--target-snr inf", "--target-snr +-1", "--target-snr 201",
            "--tx-dbm -201", "--tx-dbm 101", "--path-loss-db -1", "--path-loss-db 501",
            "--noise-dbm-hz -251", "--noise-dbm-hz 1", "--rate 0", "--rate .001", "--rate 31MHz",
            "--rate 3.6watts", "--carrier 0", "--carrier 30000001", "--dsp-workspace 90%",
            "--oscillator imaginary", "--pattern imaginary", "--rate $(touch /tmp/never)",
            "--rate 3600;echo", "--tx-dbm `whoami`"})
        rejects([&]{(void)launch_command::parse(command);},std::string("invalid command was accepted: ")+command);
    rejects([]{(void)launch_command::parse(std::string(8193,'x'));},"oversized command was accepted");
    auto invalid=launch_command::Patch{};invalid.tx_dbm=std::numeric_limits<double>::infinity();
    rejects([&]{(void)launch_command::format(invalid);},"invalid programmatic export was formatted");
}
void startup() {
    const auto invoke=[](std::vector<std::string> arguments,const std::function<int(Launch)>& run) {
        arguments.insert(arguments.begin(),"datapump-gui");std::vector<char*> argv;
        for(auto& argument:arguments)argv.push_back(argument.data());
        return gui_main(static_cast<int>(argv.size()),argv.data(),"test",run);
    };
    unsigned called=0;
    check(invoke({"--monochrome","--simulation","--auto-pattern","--target-snr=-8","--rate","3.6kHz",
                  "--carrier","1500","--dsp-workspace","75%"},[&](Launch launch) {
        ++called;check(!launch.color&&launch.simulation&&launch.settings&&launch.settings->target_db_hz==-8&&
            launch.settings->rate_hz==3600&&launch.settings->workspace_percent==75,
            "GUI startup must forward validated settings alongside existing launch flags");return 27;
    })==27&&called==1,"GUI startup callback must run once with the parsed settings");
    check(invoke({},[&](Launch launch){++called;check(!launch.settings&&!launch.simulation,
        "ordinary GUI startup defaults must remain unchanged");return 0;})==0,"empty startup failed");
    std::ostringstream errors;
    {
        struct Restore {std::streambuf* previous;~Restore(){std::cerr.rdbuf(previous);}} restore{std::cerr.rdbuf(errors.rdbuf())};
        for(const auto& arguments:std::vector<std::vector<std::string>>{{"--unknown"},{"--tx-dbm"},
                {"--rate","nan"},{"--target-snr","201"},{"stray"},{"--rate","--simulation"}})
            check(invoke(arguments,[&](Launch){++called;return 0;})==1,"invalid startup must fail before launching a backend");
    }
    check(called==2&&!errors.str().empty(),"invalid startup invoked a backend or lacked an error");
}
}
int main() {
    try {parsing_and_formatting();invalid_commands();startup();std::cout<<"GUI launch command tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"GUI launch command tests failed: "<<error.what()<<'\n';return 1;}
}
