#include "application.hpp"
#include "launch_command.hpp"
#include "gui_smoke_budget.hpp"
#include "datapump/tuning.hpp"
#include "datapump/clock_sync.hpp"
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
    const auto clock=launch_command::parse("--clock-sync GPS_0.1ms-400ms_region-2564ms_offset --audio-error 30ms --dsss-factor 1000 --fhss fake-0.4s-200 --live-duplex yes");
    check(clock.audio_timing_error_seconds==.03,"audio allowance lost duration units");
    check(clock.full_duplex==true,"duplex selection was omitted from the saved settings");
    const auto half_duplex=launch_command::parse("--live-duplex no");
    check(half_duplex.full_duplex==false && launch_command::parse(launch_command::format(half_duplex))==half_duplex,
        "explicit half duplex was confused with an omitted option");
    check(!launch_command::parse("--rate 1200").full_duplex.has_value(),"older commands must leave duplex unchanged");
    check(launch_command::parse(launch_command::format(clock))==clock,"clock and spreading settings must round trip");
    const auto policy=datapump::clock_sync::parse(*clock.clock_sync);
    check(policy && std::abs(policy->half_window_seconds()-.2002)<1e-12 && policy->offset_seconds==2.564,
          "region must be total width with BOTH stations GPS bounds outside it");
    check(datapump::clock_sync::parse("GPS_1e-3ms-1us_region-0ms_offset")->accuracy_seconds==1e-6,
          "sub-millisecond scientific clock input lost precision");
    check(!datapump::clock_sync::parse("Default"),"Default clock policy must remain absent");
    for(const auto* command:{"--fhss genuine","--fhss ic-7100","--dsss-factor 2","--clock-sync GPS_nanms-1ms_region-0ms_offset",
        "--clock-sync GPS_1ms--1ms_region-0ms_offset","--audio-error 0ms","--audio-error -1ms",
        "--audio-error NaNms","--audio-error 61s","--audio-error 30","--live-duplex maybe","--live-duplex 1"})rejects([&]{launch_command::parse(command);},"invalid clock/spreading setting accepted");
    const auto settings=launch_command::parse(
        "./datapump-gui --auto-pattern --tx-dbm 3 --path-loss-db=170 --noise-dbm-hz -164 "
        "--oscillator gpsdo-ocxo --target-snr -8 --rate 3600 --carrier 1500 --dsp-workspace 50%");
    check(settings.tx_dbm==3&&settings.path_loss_db==170&&settings.noise_dbm_hz==-164&&
          settings.target_db_hz==-8&&settings.rate_hz==3600&&settings.carrier_hz==1500&&
          settings.workspace_percent==50&&settings.oscillator=="gpsdo-ocxo"&&settings.pattern=="auto-pattern",
          "complete command did not parse its concrete settings");
    const auto command=launch_command::format(settings);
    check(launch_command::parse(command)==settings,"canonical command must round-trip every setting exactly");
    for(const auto rate:{.001,10.}) {
        auto changed=settings;changed.rate_hz=rate;
        check(launch_command::parse(launch_command::format(changed))==changed,
              "new Rate presets must round-trip exact numeric launch settings");
    }
    check(command.find("--auto-pattern")!=std::string::npos&&command.find("--target-snr -8")!=std::string::npos&&
          command.find("--rate 3600")!=std::string::npos,"canonical command must use concise readable flags and numbers");
    const auto radio=launch_command::parse("--oscillator crystal --rf-oscillator gpsdo-ocxo --carrier 10.0015MHz --shift 10MHz --search-margin 3x --reference shared-radio --sideband upper");
    check(radio.rf_shift_hz==10000000&&radio.search_margin==3&&radio.reference=="shared-radio"&&radio.rf_oscillator=="gpsdo-ocxo"&&radio.carrier_hz==10001500,
          "Radio settings did not retain separate LF/RF models and shared clocks");
    check(launch_command::parse(launch_command::format(radio))==radio,"Radio configuration did not round-trip exactly");
    const auto shorthand=launch_command::parse("--lf-reference 0Hz --rf-carrier 10.0015MHz --rf-shift 10MHz --search-margin 2");
    check(shorthand.reference=="shared-radio"&&shorthand.carrier_hz==10001500&&shorthand.rf_shift_hz==10000000,
          "LF=0 shorthand or carrier/shift aliases changed their absolute frequency meaning");
    check(launch_command::parse("--rf-shift 0 --search-margin 1").rf_shift_hz==0,"Untranslated zero-Hz RF shift was rejected");
    const auto translated=launch_command::parse("--carrier 1.0015MHz --shift 1MHz");
    check(translated.carrier_hz==1001500&&translated.rf_shift_hz==1000000,
          "absolute Carrier and Shift did not retain the translated 1.5kHz real stream");
    const auto direct=launch_command::parse("--carrier 1.0015MHz --shift 0Hz");
    check(direct.carrier_hz==1001500&&direct.rf_shift_hz==0,
          "direct real MHz carrier was reinterpreted as an audio tone");
    const auto exported=launch_command::format(translated);
    check(exported.find("--carrier ")!=std::string::npos&&exported.find("--shift ")!=std::string::npos&&
          exported.find("--rf-shift")==std::string::npos&&exported.find("--sideband")==std::string::npos&&
          launch_command::parse(exported)==translated,
          "canonical export must preserve absolute Carrier and Shift using only USB semantics");
    check(launch_command::parse("--carrier 1001500 --rf-carrier 1.0015MHz --shift 1MHz")==translated,
          "matching absolute Carrier aliases should normalize to the same patch");
    for(const auto& units:std::vector<std::pair<std::string,double>>{
        {"  1.5 KHZ  ",1500},{"\t1.0015 mHz",1001500},{"1.0000015 GhZ",1000001500},
        {"1.0000000015 THZ",1000000001500},{"1.5e-9 GHz",1.5},{"+1.5E3 hZ",1500}}) {
        const std::vector<std::string> frequency_args{"--carrier",units.first};
        const auto unit_value=launch_command::parse_arguments(frequency_args);
        check(unit_value.carrier_hz==units.second,"Frequency order units, spacing or scientific notation were rejected");
        check(launch_command::parse(launch_command::format(unit_value))==unit_value,
            "Frequency units lost precision when exported and pasted");
    }
    const auto microwave=launch_command::parse("--carrier '2.4000015 GHz' --shift '2.4 GHz'");
    const auto terahertz=launch_command::parse("--carrier 1.0000000015THz --shift 1THz");
    check(microwave.carrier_hz==2400001500&&microwave.rf_shift_hz==2400000000&&
        terahertz.carrier_hz==1000000001500&&terahertz.rf_shift_hz==1000000000000,
        "Carrier and Shift units were restricted by the real-stream rate cap");
    auto precise_frequency=microwave;
    precise_frequency.carrier_hz=std::nextafter(*microwave.carrier_hz,*microwave.carrier_hz+1);
    check(launch_command::parse(launch_command::format(precise_frequency))==precise_frequency,
        "Frequency export discarded a fractional Hz above GHz");
    auto precise=settings;precise.target_db_hz=std::nextafter(-47.,-48.);
    precise.short_target_db_hz=-20.1234567890123;precise.long_target_db_hz=23;
    check(launch_command::parse(launch_command::format(precise))==precise,
          "formatting must not change sample-sensitive target precision or individual overrides");
    for(const auto mode:datapump::tuning::pattern_modes()) {
        auto pattern=settings;pattern.pattern=datapump::tuning::pattern_mode_name(mode);
        check(launch_command::parse(launch_command::format(pattern))==pattern,"every actual pattern mode must retain its identity");
    }
    for(const auto& oscillator:datapump::tuning::oscillator_presets()) {
        auto model=settings;
        if(oscillator.id=="ic-7100")model.rf_oscillator=oscillator.id;else model.oscillator=oscillator.id;
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
            "--noise-dbm-hz -251", "--noise-dbm-hz 1", "--rate 0", "--rate .0009", "--rate 31MHz",
            "--rate 3.6watts", "--carrier 0", "--carrier 30000001 --shift 0", "--dsp-workspace 90%",
            "--rf-shift -1", "--rf-shift NaN", "--rf-shift 1e308MHz", "--shift 1e308GHz", "--carrier 1e308THz", "--rate 1GHz", "--carrier '1 2 MHz'", "--carrier '1 GHz extra'", "--carrier '+-1GHz'", "--rf-carrier 0", "--rf-shift 1 --rf-carrier 1",
            "--search-margin .99", "--search-margin inf", "--reference imaginary", "--sideband imaginary", "--sideband lower", "--carrier 1500 --shift 1MHz", "--carrier 1MHz --shift 1MHz", "--carrier 1500 --rf-carrier 10001500", "--lf-reference 1500",
            "--reference independent --lf-reference 0", "--lf-reference 0 --reference independent", "--rf-oscillator imaginary",
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
                {"--rate","nan"},{"--target-snr","201"},{"stray"},{"--rate","--simulation"},
                {"--smoke-timeout","9"},{"--smoke-timeout","1200.000001"},{"--smoke-timeout","1201"},
                {"--smoke-timeout","nan"},{"--smoke-timeout","inf"}})
            check(invoke(arguments,[&](Launch){++called;return 0;})==1,"invalid startup must fail before launching a backend");
    }
    check(called==2&&!errors.str().empty(),"invalid startup invoked a backend or lacked an error");
    for(const auto& value:{"10","600","1200"})
        check(invoke({"--smoke-test","--smoke-timeout",value},[&](Launch launch) {
            check(launch.smoke&&launch.timeout==std::stod(value),"Smoke startup changed its configured workload limit");
            return 29;
        })==29,"A bounded smoke workload allowance was rejected");
    errors.str({});errors.clear();
    {
        struct Restore {std::streambuf* previous;~Restore(){std::cerr.rdbuf(previous);}} restore{std::cerr.rdbuf(errors.rdbuf())};
        const smoke_detail::SampledWork work{13,2928175,.032,4.275,true,true,false,false,false,false,false};
        const auto exhausted=[&](Launch) -> int {throw SmokeBudgetExhausted(17,600.011,600,work,.001);};
        check(invoke({"--smoke-test","--smoke-timeout","600"},exhausted)==smoke_budget_exit_code,
            "Typed advancing-work exhaustion did not retain its dedicated nonzero exit");
        check(errors.str()==std::string(smoke_budget_marker)+
            "phase=17 elapsed=600.011000 budget=600.000000 tx_id=13 fraction=0.032000 media_seconds=4.275000 samples=2928175 tail=0 progress_age=0.001000 result=incomplete\n",
            "Smoke startup did not emit exactly one structured incomplete diagnostic");
        errors.str({});errors.clear();
        check(invoke({"--smoke-test","--smoke-timeout","1200"},[&](Launch launch) -> int {
            throw SmokeBudgetExhausted(17,1200.011,launch.timeout,work,.001);
        })==smoke_budget_exit_code&&errors.str().find("budget=1200.000000")!=std::string::npos,
            "The extended smoke allowance swallowed its nonzero incomplete result");
        errors.str({});errors.clear();
        check(invoke({},exhausted)==1&&errors.str().starts_with("Data Pump test: ")&&
            errors.str().find(smoke_budget_marker)==std::string::npos,
            "Ordinary startup acquired a smoke-only advisory result");
        errors.str({});errors.clear();
        check(invoke({"--smoke-test"},[](Launch) -> int {
            throw datapump::Error("Shared GUI smoke timed out in phase 17: stalled work");
        })==1&&errors.str().starts_with("Data Pump test: "),
            "An ordinary timeout was misclassified as a typed workload result");
        errors.str({});errors.clear();
        check(invoke({"--smoke-test"},[](Launch) -> int {throw std::runtime_error("Assertion failed");})==1&&
            errors.str()=="Data Pump test: Assertion failed\n",
            "A correctness failure was downgraded to a workload result");
    }
}
}
int main() {
    try {parsing_and_formatting();invalid_commands();startup();std::cout<<"GUI launch command tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"GUI launch command tests failed: "<<error.what()<<'\n';return 1;}
}
