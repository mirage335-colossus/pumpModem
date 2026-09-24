#include "datapump/tuning.hpp"
#include "datapump/transfer.hpp"
#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
template<class F> void rejects(F action,const char* text) {
    try {action();} catch(const Error&) {return;}
    throw std::runtime_error(text);
}
void near(double a,double b,const char* text) {check(std::abs(a-b)<1e-8,text);}
void changing_pattern(const modem::Config& config) {
    check(config.spreading_mode==modem::SpreadingMode::pattern,"automatic waveform checks require changing patterns");
    modem::PatternCode code(config);
    const auto first=code.value(0,0);bool changes=false;
    for(unsigned i=1;i<64;++i)changes|=code.value(i,0)!=first;
    check(changes,"pattern profile must produce changing chips");
}
void modes_and_patterns() {
    check(tuning::pattern_modes().size()==19,"exact number of pattern choices");
    for(const auto mode:tuning::pattern_modes())
        check(tuning::parse_pattern_mode(tuning::pattern_mode_name(mode))==mode,"pattern name roundtrip");
    // Forced tones are manual hardware experiments. Automated modulation and
    // acquisition use chip patterns with observable phase changes instead.
    for(const auto mode:{tuning::PatternMode::auto_pattern,tuning::PatternMode::auto_keystream,
                         tuning::PatternMode::pattern_3,tuning::PatternMode::pattern_4,
                         tuning::PatternMode::pattern_6,tuning::PatternMode::pattern_8,
                         tuning::PatternMode::pattern_12,tuning::PatternMode::pattern_16}) {
        const auto plan=tuning::resolve(24000,40,mode,true);
        modem::validate(plan.config);
        check(plan.config.scramble,"every keyed pattern mode needs private acquisition evidence");
        changing_pattern(plan.config);
    }
    rejects([]{tuning::parse_pattern_mode("pattern-7");},"reject unlisted pattern");
    rejects([]{tuning::parse_pattern_mode("tone-64");},"reject unlisted tone");
    auto pattern=tuning::resolve(24000,100,tuning::PatternMode::pattern_8,false).config;
    check(!pattern.scramble,"forced plaintext patterns do not turn on keystream");
    changing_pattern(pattern);
    for(const auto mode:tuning::pattern_modes()) {
        const auto name=tuning::pattern_mode_name(mode);
        if(name!="auto-tone" && !name.starts_with("tone-"))continue;
        const auto plan=tuning::resolve(1200,40,mode,true);
        check(plan.config.spreading_mode==modem::SpreadingMode::tone && !plan.config.scramble &&
              !plan.config.dsss && !plan.config.data_key,
              "tone planning must force all private modulation off");
        check(plan.explanation.find("unencrypted")!=std::string::npos &&
              plan.explanation.find("Low-Probability-of-Intercept")!=std::string::npos,
              "tone planning must identify its lack of encryption and LPI protection");
    }
}
void snr_planning() {
    auto plan=tuning::resolve(1200,6,tuning::PatternMode::auto_pattern,false);
    check(plan.config.pattern_symbols && plan.config.constellation_bits==1,"automatic planning carries one meaningful bit per rare pattern");
    near(plan.required_spreading,std::pow(10.,(plan.target_symbol_snr_db-6)/10)*600.,"required spreading from selected symbol energy");
    near(plan.estimated_processing_gain_db,10*std::log10(static_cast<double>(plan.config.spreading_factor)),"spreading gain estimate");
    near(plan.estimated_symbol_snr_db,6+10*std::log10(modem::symbol_seconds(plan.config)),"C/N0 to symbol SNR");
    check(plan.target_supported && plan.estimated_symbol_snr_db>=plan.target_symbol_snr_db,"auto meets its stated engineering target");
    plan=tuning::resolve(1200,-20,tuning::PatternMode::auto_keystream,true);
    check(plan.target_supported && plan.config.integration_seconds>16384./600 && plan.config.scramble,"automatic integration extends beyond the finite chip template");
    check(plan.estimated_symbol_snr_db>=plan.target_symbol_snr_db,"long automatic integration meets its numeric target");
    rejects([]{tuning::resolve(1200,-270,tuning::PatternMode::auto_keystream,true);},"duration beyond 64-bit sample counters is rejected explicitly");
    plan=tuning::resolve(1200,6,tuning::PatternMode::auto_keystream,false);
    check(!plan.config.scramble && plan.config.spreading_mode==modem::SpreadingMode::pattern,"no-key auto fallback");
    plan=tuning::resolve(1200,-60,tuning::PatternMode::pattern_3,true);
    check(!plan.target_supported && plan.config.spreading_factor==3,"explicit mode retained even if target impossible");
    rejects([]{tuning::resolve(0,6,tuning::PatternMode::auto_pattern,false);},"invalid bandwidth");
    rejects([]{tuning::resolve(30000001,6,tuning::PatternMode::auto_pattern,false);},"unsupported modem bandwidth");
    rejects([]{tuning::resolve(1200,std::numeric_limits<double>::quiet_NaN(),tuning::PatternMode::auto_pattern,false);},"invalid target SNR");
}
void bandwidth_derived_clocks() {
    for(const double bandwidth:{1.,16.,100.,100.25,1200.,1499.,1499.25,1500.,1501.,1703.,1800.,2000.,2000.25,2400.,24000.,192000.,1000000.,30000000.}) {
        const auto plan=tuning::resolve(bandwidth,150,tuning::PatternMode::auto_pattern,false);
        const auto carrier=std::max(1500.,.75*bandwidth);
        const auto expected=static_cast<std::uint32_t>(std::ceil(std::max(4*bandwidth,4*carrier)));
        check(plan.config.sample_rate==expected,"internal sample clock must cover occupied bandwidth and the audio carrier");
        near(plan.config.carrier_hz,carrier,"low-bandwidth audio must use a usable carrier");
        check(plan.config.carrier_hz-bandwidth/2>=300,"automatic audio band extends below the usable audio range");
        check(plan.config.carrier_hz+bandwidth/2<.42*plan.config.sample_rate,"internal spectrum must fit the conversion passband");
        check(plan.config.spreading_factor>=64,"strong links retain enough chips for standalone pattern evidence");
        near(modem::bit_rate(plan.config),bandwidth/128,"strong channel rate preserves the 64-chip confidence floor");
        const auto budget=tuning::link_budget(tuning::simulation_presets()[1],bandwidth,plan.config.sample_rate);
        check(std::isfinite(budget.sample_snr_db),"low and SDR-rate clocks need valid link budgets");
    }
    const modem::Config defaults;
    near(defaults.carrier_hz,tuning::recommended_carrier_hz(defaults.bandwidth_hz),"raw default carrier differs from the automatic carrier");
    check(defaults.sample_rate==tuning::recommended_sample_rate(defaults.bandwidth_hz),"raw default clock differs from the automatic clock");
}
void audio_passband_pattern_roundtrips() {
    // Exercise real PCM with non-integer carrier cycles per chip using the
    // same one-bit patterns and integration floor as automatic plans.
    Message message;message.data=Bytes{'e'};
    for(const double bandwidth:{100.,1200.,1499.,1499.25,1703.,1800.}) {
        transfer::Options options;
        options.modem=tuning::resolve(bandwidth,100,tuning::PatternMode::auto_pattern,false).config;
        options.timestamp=1800000000;options.search_seconds=0;
        const auto decoded=transfer::receive(transfer::transmit(message,options),options);
        check(decoded.packet.message.data==message.data && decoded.raw_bits==Bytes({0,0,1}),
              "audio passband corrupted exact pattern bits");
    }
}
void automatic_pattern_rates() {
    const auto fast=tuning::resolve(2400,100,tuning::PatternMode::auto_pattern,false);
    check(fast.config.pattern_symbols && fast.config.constellation_bits==1 && fast.config.spreading_factor==64,"strong links preserve sparse pattern evidence");
    near(modem::bit_rate(fast.config),18.75,"high-C/N0 rate includes the minimum pattern length");
    const auto weak=tuning::resolve(2400,-20,tuning::PatternMode::auto_pattern,false);
    const auto weaker=tuning::resolve(2400,-30,tuning::PatternMode::auto_pattern,false);
    check(weak.config.constellation_bits==1,"weak links integrate evidence for each raw bit");
    near(modem::symbol_seconds(weaker.config)/modem::symbol_seconds(weak.config),10,"ten-dB weaker automatic mode integrates ten times longer");
    auto clock=fast.config;clock.bandwidth_hz=1703;clock.spreading_factor=128;
    clock.integration_seconds=0;
    const auto seconds=modem::symbol_seconds(clock),rate=modem::bit_rate(clock);
    for(const auto sample_rate:{44100U,48000U,96000U,192000U}) {
        clock.sample_rate=sample_rate;
        near(modem::symbol_seconds(clock),seconds,"nominal symbol clock depends only on bandwidth and spreading");
        near(modem::bit_rate(clock),rate,"modem throughput depends on hardware sample rate");
        const auto rendered=static_cast<double>(modem::symbol_sample_count(clock))/sample_rate;
        check(rendered>=seconds-1e-12 && rendered-seconds<=1./sample_rate,"PCM boundary quantization exceeds one sample");
    }
    clock.constellation_bits=7;rejects([&]{modem::validate(clock);},"multi-bit symbol transport accepted");
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
    transfer::Options options;
    options.modem=tuning::resolve(24000,100,tuning::PatternMode::auto_pattern,false).config;
    const Bytes bits{0,1,0};
    const auto estimate=transfer::estimate_binary(bits,options);
    auto transmitter=transfer::binary_transmitter(bits,options);
    check(estimate.waveform_samples==transmitter->total_samples(),"allocation-free sample count is exact");
    check(estimate.memory_supported,"ordinary pattern transmission memory is supported");
    auto removed=options.modem;removed.pattern_symbols=false;
    rejects([&]{modem::validate(removed);},"removed APSK transport was accepted");
    options.modem.memory_limit=std::numeric_limits<std::size_t>::max();
    rejects([&]{modem::waveform_sample_count(std::numeric_limits<std::size_t>::max(),options.modem);},
            "sample count overflow rejected before allocation");
}
void receive_target_lists() {
    const auto valid=tuning::parse_receive_targets(" 40, +6, -6, 40.0, 6e0, -0 ");
    check(!valid.reset && valid.values==std::vector<double>({40,6,-6,0}) && valid.canonical=="40, 6, -6, 0",
          "receive list parsing must trim, deduplicate and canonicalize without changing order");
    for(const auto* input:{"", "40,", ",40", "40,,6", "40, nonsense", "nan", "inf", "1e999", "201", "-201", "40 dB", "0x40", "+-6"}) {
        const auto rejected=tuning::parse_receive_targets(input);
        check(rejected.reset && rejected.values==std::vector<double>{40} && rejected.canonical=="40",
              "one invalid target must reset the entire receive list");
    }
    check(tuning::parse_receive_targets(std::string(513,' ')).reset,"receive target text exceeds its bounded parser");
    std::string many="40";for(unsigned i=1;i<=16;++i)many+=",40";
    check(tuning::parse_receive_targets(many).reset,"repeated entries cannot bypass the target count bound");
    const std::array<double,4> targets{40,100,6,-6};
    const auto profiles=tuning::receive_profiles(1200,targets,tuning::PatternMode::auto_pattern,false);
    check(profiles.size()==3,"targets with identical actual waveform profiles must share a receiver");
    for(const auto& profile:profiles)
        check(profile.bandwidth_hz==1200 && profile.spreading_mode==modem::SpreadingMode::pattern && !profile.scramble,
              "receive targets must not search other bandwidths or pattern modes");
    const auto keyed_profiles=tuning::receive_profiles(1200,targets,tuning::PatternMode::auto_pattern,true);
    check(keyed_profiles.size()==profiles.size() && std::all_of(keyed_profiles.begin(),keyed_profiles.end(),[](const auto& c){return c.scramble;}),
          "default keyed receive profiles must distinguish keys using pattern evidence");
    check(tuning::receive_profiles(1200,targets,tuning::PatternMode::pattern_8,false).size()==1,
          "forced length profiles must deduplicate independently of target labels");
    auto customized=profiles.front();customized.sample_rate=8000;customized.carrier_hz=1750;
    customized.memory_limit=2*1024*1024;customized.dsss=true;customized.stream_epoch=12345;
    customized.spreading_seed.fill(7);customized.dsss_seed.fill(11);
    const auto preserved=tuning::receive_profiles(customized,targets,tuning::PatternMode::auto_keystream,true);
    check(preserved.size()==3,"custom-clock duplicate waveform profiles were not merged");
    for(const auto& profile:preserved)
        check(profile.sample_rate==8000 && profile.carrier_hz==1750 && profile.dsss && profile.scramble &&
              profile.memory_limit==customized.memory_limit && profile.stream_epoch==customized.stream_epoch &&
              profile.spreading_seed==customized.spreading_seed && profile.dsss_seed==customized.dsss_seed,
              "receive search discarded the caller's clock, carrier, spreading stream or resource configuration");
    customized.data_key.emplace(Bytes(32,0x31));
    for(const auto mode:{tuning::PatternMode::auto_tone,tuning::PatternMode::tone_128}) {
        const auto tones=tuning::receive_profiles(customized,targets,mode,true);
        for(const auto& profile:tones) {
            check(profile.spreading_mode==modem::SpreadingMode::tone && !profile.scramble &&
                  !profile.dsss && !profile.data_key &&
                  profile.spreading_seed==std::array<std::uint8_t,32>{} &&
                  profile.dsss_seed==std::array<std::uint8_t,32>{},
                  "tone receive profile inherited private streams from a keyed base");
            check(profile.sample_rate==customized.sample_rate && profile.carrier_hz==customized.carrier_hz &&
                  profile.memory_limit==customized.memory_limit,
                  "tone normalization changed the caller's hardware or resource settings");
        }
    }
    customized.data_key.reset();
    transfer::Options custom_receive;custom_receive.modem=customized;custom_receive.modem.dsss=false;
    custom_receive.automatic_receive_profiles=true;custom_receive.timestamp=1800000000;custom_receive.search_seconds=0;
    Message tiny;tiny.data=Bytes{'e'};
    const auto actual=transfer::receive(transfer::transmit(tiny,custom_receive),custom_receive);
    check(actual.packet.message.data==tiny.data && actual.raw_bits==Bytes({0,0,1}) && !actual.packet_validated,
          "automatic receive profiles lost an explicit carrier or PCM clock during physical short-bit recovery");
    rejects([]{tuning::receive_profiles(1200,{},tuning::PatternMode::auto_pattern,false);},"empty programmatic target list accepted");
    const auto forced=tuning::resolve(1200,100,tuning::PatternMode::pattern_8,false);
    check(!forced.target_supported && forced.config.spreading_factor==8 && forced.config.pattern_symbols,
          "forced short patterns must remain available with unsupported standalone confidence");
    const transfer::Options defaults;
    check(defaults.receive_targets_db_hz==std::vector<double>{40} && !defaults.automatic_receive_profiles,
          "manual API configurations must preserve their explicit profile by default");
}
}
int main() {
    try {modes_and_patterns();snr_planning();receive_target_lists();automatic_pattern_rates();bandwidth_derived_clocks();audio_passband_pattern_roundtrips();physical_simulation_presets();sizing_and_validation();std::cout<<"tuning tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"tuning tests failed: "<<error.what()<<'\n';return 1;}
}
