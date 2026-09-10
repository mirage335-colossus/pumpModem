#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
template<class F> void rejects(F action,const char* text) {
    try {action();} catch(const Error&) {return;}
    throw std::runtime_error(text);
}
void near(double a,double b,const char* text) {check(std::abs(a-b)<1e-8,text);}
void modes_and_tones() {
    check(tuning::pattern_modes().size()==19,"exact number of pattern choices");
    for(const auto mode:tuning::pattern_modes()) {
        check(tuning::parse_pattern_mode(tuning::pattern_mode_name(mode))==mode,"pattern name roundtrip");
        const auto plan=tuning::resolve(24000,100,mode,true);
        modem::validate(plan.config);
        const Bytes bits{0,1,1,0};
        const auto wave=modem::modulate_status(bits,plan.config);
        check(modem::detect_status(wave,bits,plan.config)>.999,"all explicit patterns preserve positive and negative status symbols");
    }
    rejects([]{tuning::parse_pattern_mode("pattern-7");},"reject unlisted pattern");
    rejects([]{tuning::parse_pattern_mode("tone-64");},"reject unlisted tone");
    auto tone=tuning::resolve(24000,100,tuning::PatternMode::tone_8,false).config;
    auto pattern=tuning::resolve(24000,100,tuning::PatternMode::pattern_8,false).config;
    check(tone.spreading_mode==modem::SpreadingMode::tone,"tone mode resolved");
    check(!tone.scramble && !pattern.scramble,"forced plaintext modes do not turn on keystream");
    const auto tone_wave=modem::modulate_status(Bytes{0},tone);
    const auto pattern_wave=modem::modulate_status(Bytes{0},pattern);
    check(tone_wave!=pattern_wave,"tone is an actual waveform change");
    check(modem::detect_status(tone_wave,Bytes{0},pattern)<.1,"fixed pattern has distinct phase chips");
    for(const auto mode:{tuning::PatternMode::tone_3,tuning::PatternMode::pattern_3,tuning::PatternMode::pattern_16}) {
        auto config=tuning::resolve(24000,100,mode,false).config;
        auto preamble=modem::preamble(config),wire=preamble;
        wire.insert(wire.end(),{0,0xff,0x35,0xa8});
        check(modem::demodulate(modem::modulate(wire,config),config,preamble).bytes==wire,"tone and pattern packet roundtrip");
    }
}
void snr_planning() {
    auto plan=tuning::resolve(1200,6,tuning::PatternMode::auto_pattern,false);
    check(plan.config.spreading_factor==16384,"C/N0 integration selects sufficient finite spreading");
    near(plan.required_spreading,std::pow(10.,1.2)*600.,"required spreading from symbol energy");
    near(plan.estimated_processing_gain_db,10*std::log10(16384.),"spreading gain estimate");
    near(plan.estimated_symbol_snr_db,6+10*std::log10(modem::symbol_seconds(plan.config)),"C/N0 to symbol SNR");
    check(plan.target_supported && plan.estimated_symbol_snr_db>=plan.target_symbol_snr_db,"auto meets its stated engineering target");
    plan=tuning::resolve(1200,-20,tuning::PatternMode::auto_keystream,true);
    check(plan.target_supported && plan.config.integration_seconds>16384./600 && plan.config.scramble,"automatic integration extends beyond the finite chip template");
    check(plan.estimated_symbol_snr_db>=plan.target_symbol_snr_db,"long automatic integration meets its numeric target");
    rejects([]{tuning::resolve(1200,-270,tuning::PatternMode::auto_keystream,true);},"duration beyond 64-bit sample counters is rejected explicitly");
    plan=tuning::resolve(1200,6,tuning::PatternMode::auto_keystream,false);
    check(!plan.config.scramble && plan.config.spreading_mode==modem::SpreadingMode::pattern,"no-key auto fallback");
    plan=tuning::resolve(1200,-60,tuning::PatternMode::tone_1,true);
    check(!plan.target_supported && plan.config.spreading_factor==1,"explicit mode retained even if target impossible");
    rejects([]{tuning::resolve(0,6,tuning::PatternMode::auto_pattern,false);},"invalid bandwidth");
    rejects([]{tuning::resolve(1000000,6,tuning::PatternMode::auto_pattern,false);},"unsupported audio bandwidth");
    rejects([]{tuning::resolve(1200,std::numeric_limits<double>::quiet_NaN(),tuning::PatternMode::auto_pattern,false);},"invalid target SNR");
}
void physical_simulation_presets() {
    check(tuning::simulation_presets().size()==11,"all specified simulation presets");
    check(!tuning::parse_simulation_preset("no").enabled,"simulation defaults off");
    auto preset=tuning::parse_simulation_preset("3dBm -170dB");
    const auto result=tuning::link_budget(preset,1200,48000);
    near(result.received_power_dbm,-167,"power plus attenuation");
    near(result.noise_power_dbm,-164+10*std::log10(1200.),"thermal floor and noise figure");
    near(result.snr_db_hz,-3,"C/N0 from physical powers");
    near(result.snr_db,-3-10*std::log10(1200.),"in-band SNR");
    near(result.sample_snr_db,-3-10*std::log10(24000.),"AWGN SNR uses sampled Nyquist noise bandwidth");
    near(tuning::link_budget(preset,1200,96000).sample_snr_db,result.sample_snr_db-10*std::log10(2.),"sample-rate-independent physical PSD");
    check(tuning::parse_simulation_preset("50dbm-270db").transmit_dbm==50,"stable compact preset spelling");
    rejects([]{tuning::parse_simulation_preset("3dBm -7dB");},"reject unlisted preset");
}
void sizing_and_validation() {
    auto config=tuning::resolve(24000,100,tuning::PatternMode::tone_3,false).config;
    const auto training=modem::preamble(config);
    auto bytes=training;bytes.insert(bytes.end(),{1,2,3});
    const auto wave=modem::modulate(bytes,config);
    check(modem::waveform_sample_count(bytes.size(),config)==wave.size(),"allocation-free sample count is exact");
    check(modem::memory_supported(bytes.size(),training.size(),config),"ordinary acquisition memory supported");
    config.memory_limit=1024;
    check(!modem::memory_supported(bytes.size(),training.size(),config),"memory estimate rejects bounded waveform");
    check(!modem::memory_supported(0,0,config),"empty estimated preamble rejected");
    config.memory_limit=std::numeric_limits<std::size_t>::max();
    rejects([&]{modem::waveform_sample_count(std::numeric_limits<std::size_t>::max(),config);},"sample count overflow rejected before allocation");
    check(!modem::memory_supported(std::numeric_limits<std::size_t>::max(),training.size(),config),"memory overflow reported as unsupported");
    config.scramble=true;
    rejects([&]{modem::validate(config);},"tone cannot accidentally select keystream chips");
}
}
int main() {
    try {modes_and_tones();snr_planning();physical_simulation_presets();sizing_and_validation();std::cout<<"tuning tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"tuning tests failed: "<<error.what()<<'\n';return 1;}
}
