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
std::vector<float> raw_capture(const Bytes& bits,const transfer::Options& options) {
    auto source=transfer::binary_transmitter(bits,options);
    std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
    std::size_t offset=0;
    while(!source->finished())offset+=source->read(std::span(samples).subspan(offset));
    samples.resize(samples.size()+modem::pattern_absence_samples(options.modem)+
                   options.modem.sample_rate+2*modem::symbol_sample_count(options.modem));
    return samples;
}
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
void shannon_capacity() {
    // Fixed reference values use C/N0 targets, not in-band dB. At 1 kHz,
    // 30 dB-Hz means equal signal and in-band noise powers: exactly 1 bit/s/Hz.
    near(tuning::shannon_capacity_bps(1,0),1,"one-hertz zero-dB capacity");
    near(tuning::shannon_capacity_bps(1000,30),1000,"C/N0 must be converted to in-band SNR");
    near(tuning::shannon_capacity_bps(1000,40),3459.4316186372973,"ten-dB in-band capacity");
    near(tuning::shannon_capacity_bps(1000,20),137.5035237499349,"negative in-band dB capacity");
    near(tuning::shannon_capacity_bps(1000,60),9967.226258835993,"strong-signal capacity");
    near(tuning::shannon_capacity_bps(10000,60),66582.11482751794,"bandwidth changes noise power at fixed C/N0");
    // Relative comparison prevents the ordinary absolute tolerance from
    // accepting zero at targets where adding linear SNR to one rounds to one.
    near(tuning::shannon_capacity_bps(30000000,-200)/1.4426950408889634e-20,1,
         "weak-signal capacity must retain precision");
    near(tuning::shannon_capacity_bps(1000,4000)/1318805.4536702829,1,
         "strong finite targets must not overflow while converting dB");
    for(const auto bandwidth:{0.,.5,30000001.,std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()})
        rejects([&]{tuning::shannon_capacity_bps(bandwidth,60);},"invalid capacity bandwidth accepted");
    for(const auto target:{std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
        rejects([&]{tuning::shannon_capacity_bps(1000,target);},"nonfinite capacity target accepted");
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
        check(plan.config.spreading_factor>=16 && plan.config.spreading_factor<=64,
              "strong links retain a conservative pattern floor");
        check(plan.config.spreading_factor==64 ||
              modem::symbol_sample_count(plan.config)%modem::pattern_chip_samples(plan.config)==0,
              "automatic shortening introduced a periodic partial-chip hold");
        near(modem::bit_rate(plan.config),bandwidth/(2*plan.config.spreading_factor),
             "strong channel rate uses the selected whole-chip pattern floor");
        const auto budget=tuning::link_budget(tuning::simulation_presets()[1],bandwidth,plan.config.sample_rate);
        check(std::isfinite(budget.sample_snr_db),"low and SDR-rate clocks need valid link budgets");
    }
    const modem::Config defaults;
    near(defaults.carrier_hz,tuning::recommended_carrier_hz(defaults.bandwidth_hz),"raw default carrier differs from the automatic carrier");
    check(defaults.sample_rate==tuning::recommended_sample_rate(defaults.bandwidth_hz),"raw default clock differs from the automatic clock");
}
void explicit_carrier_planning() {
    near(tuning::recommended_carrier_hz(3600),2700,"GUI audio defaults must not change the legacy carrier recommendation");
    for(const bool keyed:{false,true}) {
        const auto plan=tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,keyed,1500);
        check(plan.config.carrier_hz==1500 && plan.config.sample_rate==14400 &&
              plan.config.spreading_factor==16 && plan.target_supported && plan.config.scramble==keyed,
              "centered audio planning must preserve the chosen carrier and confidence geometry");
        near(modem::bit_rate(plan.config),112.5,"centered audio changed the nominal symbol rate");
        const auto profiles=tuning::receive_profiles(plan.config,std::array<double,2>{80,100},
            tuning::PatternMode::auto_pattern,keyed);
        check(profiles.size()==1 && profiles.front().carrier_hz==1500 &&
              profiles.front().sample_rate==14400 && profiles.front().spreading_factor==16,
              "receive targets must replan the selected centered audio profile");
    }
    const auto high=tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,false,20000);
    check(high.config.carrier_hz==20000 && high.config.sample_rate==80000,
          "an explicit high carrier must increase the internal real-PCM clock");
    const auto wide=tuning::resolve(30000000,80,tuning::PatternMode::auto_pattern,false,25000000);
    check(wide.config.carrier_hz==25000000 && wide.config.sample_rate==120000000,
          "explicit carriers must retain the general-purpose high-bandwidth range");
    // The legacy 600-Hz private recommendation uses orthogonal 5-sample bins
    // and allows 32 chips. This caller's actual short-sample clock allows 16.
    auto actual=tuning::resolve(600,80,tuning::PatternMode::auto_pattern,true,400).config;
    check(actual.sample_rate==2400 && actual.spreading_factor==16,"custom clock must determine the transmitter floor");
    check(tuning::receive_profiles(actual,std::array<double,1>{80},tuning::PatternMode::auto_pattern,true)
          .front().spreading_factor==16,"a default carrier floor must not leak into a custom receive profile");
    auto orthogonal=tuning::resolve(3200,80,tuning::PatternMode::auto_pattern,true,1600);
    check(orthogonal.config.spreading_factor==32 && orthogonal.target_supported,
          "explicit orthogonal private carriers must retain the validated 32-chip floor");
    // Valid explicitly sampled receiver configurations need not obey the
    // recommended clock's four-samples-per-carrier heuristic.
    auto high_receiver=wide.config;high_receiver.carrier_hz=40000000;
    check(tuning::receive_profiles(high_receiver,std::array<double,1>{80},tuning::PatternMode::auto_pattern,false)
          .front().carrier_hz==40000000,"receive planning rejected a valid custom carrier clock");
    for(const auto carrier:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),30000001.})
        rejects([&]{tuning::resolve(3600,80,tuning::PatternMode::auto_pattern,false,carrier);},
                "invalid explicit carrier accepted");
    rejects([]{tuning::resolve(3600,80,tuning::PatternMode::pattern_8,false,1500);},
            "a short rectangular pattern inherited the shaped audio extent");
    rejects([]{tuning::resolve(3600,80,tuning::PatternMode::auto_tone,false,1500);},
            "an unshaped tone profile inherited the shaped audio extent");
}
void audio_passband_pattern_roundtrips() {
    // Exercise real PCM with non-integer carrier cycles per chip using the
    // same one-bit patterns and integration floor as automatic plans.
    const Bytes bits{0,0,1};
    for(const double bandwidth:{100.,1200.,1499.,1499.25,1703.,1800.}) {
        transfer::Options options;
        options.modem=tuning::resolve(bandwidth,100,tuning::PatternMode::auto_pattern,false).config;
        options.timestamp=1800000000;options.search_seconds=0;
        const auto decoded=transfer::receive(raw_capture(bits,options),options);
        check(decoded.stream_complete && decoded.raw_bits==bits && !decoded.content_validated,
              "audio passband corrupted exact pattern bits");
    }
}
void automatic_pattern_rates() {
    const auto fast=tuning::resolve(2400,100,tuning::PatternMode::auto_pattern,false);
    check(fast.config.pattern_symbols && fast.config.constellation_bits==1 && fast.config.spreading_factor==16,"strong links preserve complete pattern evidence");
    near(modem::bit_rate(fast.config),75,"high-C/N0 rate includes the conservative minimum pattern length");
    for(const bool keyed:{false,true}) {
        const auto strong=tuning::resolve(12000,80,tuning::PatternMode::auto_pattern,keyed);
        check(strong.target_supported && strong.config.scramble==keyed,"fast planning must retain private acquisition when keyed");
        near(modem::bit_rate(strong.config),375,"12 kHz strong link removes the fixed 64-chip bottleneck");
        const auto threshold=10*std::log10(12000.);
        for(const auto [snr,expected]:std::array<std::pair<double,unsigned>,5>{
                {{threshold+23.999,64},{threshold+24,32},{threshold+29.999,32},
                 {threshold+30,16},{200,16}}}) {
            const auto plan=tuning::resolve(12000,snr,tuning::PatternMode::auto_pattern,keyed);
            check(plan.config.spreading_factor==expected && plan.target_supported,
                  "short-pattern selection must use in-band headroom and retain its floor at extreme SNR");
        }
        // Equal C/N0 at a much wider bandwidth is not the same strong channel.
        check(tuning::resolve(30000000,80,tuning::PatternMode::auto_pattern,keyed).config.spreading_factor==64,
              "C/N0 alone cannot justify shorter patterns at every bandwidth");
    }
    check(tuning::resolve(12000,200,tuning::PatternMode::auto_tone,false).config.spreading_factor==64,
          "high-SNR pattern changes must preserve automatic tone planning");
    check(tuning::resolve(1000,80,tuning::PatternMode::auto_pattern,true).config.spreading_factor==32,
          "compact orthogonal private searches need the validated 32-chip floor");
    check(tuning::resolve(1000,80,tuning::PatternMode::auto_pattern,false).config.spreading_factor==16,
          "public exact sample fits retain the validated 16-chip floor");
    for(const bool keyed:{false,true})
        check(tuning::resolve(400,80,tuning::PatternMode::auto_pattern,keyed).config.spreading_factor==64,
              "unvalidated long-sample compact profiles must retain the previous floor");
    check(tuning::resolve(600,80,tuning::PatternMode::auto_pattern,false).config.spreading_factor==64 &&
          tuning::resolve(600,80,tuning::PatternMode::auto_pattern,true).config.spreading_factor==32,
          "long-sample shortening is limited to the orthogonal private path");
    check(tuning::resolve(750,80,tuning::PatternMode::auto_pattern,false).config.spreading_factor==16,
          "the exact sample-fit upper boundary remains eligible for shortening");
    check(tuning::resolve(12000,80,tuning::PatternMode::pattern_16,true).target_supported,
          "forced 16-chip patterns can meet the same high-SNR planning floor");
    check(!tuning::resolve(12000,60,tuning::PatternMode::pattern_16,true).target_supported,
          "forced short patterns still report insufficient in-band headroom");
    for(const auto bandwidth:{100.25,1499.25})for(const bool keyed:{false,true}) {
        const auto partial=tuning::resolve(bandwidth,100,tuning::PatternMode::auto_pattern,keyed);
        check(partial.config.spreading_factor==64,
              "a high target must not introduce short-hold timing structure at fractional bandwidths");
    }
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
        check(rejected.reset && rejected.values==std::vector<double>{60} && rejected.canonical=="60",
              "one invalid target must reset the entire receive list");
    }
    check(tuning::parse_receive_targets(std::string(513,' ')).reset,"receive target text exceeds its bounded parser");
    std::string many="40";for(unsigned i=1;i<=16;++i)many+=",40";
    check(tuning::parse_receive_targets(many).reset,"repeated entries cannot bypass the target count bound");
    const std::array<double,4> targets{40,100,6,-6};
    const auto profiles=tuning::receive_profiles(1200,targets,tuning::PatternMode::auto_pattern,false);
    check(profiles.size()==4,"distinct high-SNR integrations must remain distinct receiver profiles");
    check(tuning::receive_profiles(1200,std::array<double,3>{80,100,150},tuning::PatternMode::auto_pattern,false).size()==1,
          "strong targets with identical waveforms must still share one receiver");
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
    check(preserved.size()==3,"custom-clock profiles must merge when alignment restores the prior floor");
    for(const auto& profile:preserved)
        check(profile.sample_rate==8000 && profile.carrier_hz==1750 && profile.dsss && profile.scramble &&
              profile.memory_limit==customized.memory_limit && profile.stream_epoch==customized.stream_epoch &&
              profile.spreading_seed==customized.spreading_seed && profile.dsss_seed==customized.dsss_seed,
              "receive search discarded the caller's clock, carrier, spreading stream or resource configuration");
    auto fractional_clock=tuning::resolve(12000,80,tuning::PatternMode::auto_pattern,true).config;
    fractional_clock.sample_rate=44100;
    const std::array<double,1> high_target{80};
    auto orthogonal_clock=fractional_clock;orthogonal_clock.sample_rate=48000;orthogonal_clock.carrier_hz=12000;
    check(tuning::receive_profiles(orthogonal_clock,high_target,tuning::PatternMode::auto_pattern,true).front().spreading_factor==32,
          "private confidence floor must use the caller's actual carrier geometry");
    auto dsss_only=tuning::resolve(1000,80,tuning::PatternMode::auto_pattern,false).config;dsss_only.dsss=true;
    const auto dsss_profiles=tuning::receive_profiles(dsss_only,high_target,tuning::PatternMode::auto_pattern,false);
    check(dsss_profiles.front().spreading_factor==32 && dsss_profiles.front().dsss && !dsss_profiles.front().scramble,
          "DSSS-only private patterns need the same compact-path confidence floor");
    const auto aligned=tuning::receive_profiles(fractional_clock,high_target,tuning::PatternMode::auto_pattern,true);
    check(aligned.size()==1 && aligned.front().spreading_factor==64 && aligned.front().sample_rate==44100,
          "automatic receive planning must check chip alignment against the preserved actual clock");
    check(tuning::receive_profiles(fractional_clock,high_target,tuning::PatternMode::pattern_16,true).front().spreading_factor==16,
          "alignment protection must not alter an explicitly forced manual pattern");
    fractional_clock.sample_rate=0;
    rejects([&]{tuning::receive_profiles(fractional_clock,high_target,tuning::PatternMode::auto_pattern,true);},
            "custom-clock alignment must reject invalid clocks before chip modulo arithmetic");
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
    const Bytes bits{0,0,1};
    const auto actual=transfer::receive(raw_capture(bits,custom_receive),custom_receive);
    check(actual.stream_complete && actual.raw_bits==bits && !actual.content_validated,
          "automatic receive profiles lost an explicit carrier or PCM clock during physical short-bit recovery");
    rejects([]{tuning::receive_profiles(1200,{},tuning::PatternMode::auto_pattern,false);},"empty programmatic target list accepted");
    const auto forced=tuning::resolve(1200,100,tuning::PatternMode::pattern_8,false);
    check(!forced.target_supported && forced.config.spreading_factor==8 && forced.config.pattern_symbols,
          "forced short patterns must remain available with unsupported standalone confidence");
    const transfer::Options defaults;
    check(defaults.receive_targets_db_hz==std::vector<double>{60} && !defaults.automatic_receive_profiles,
          "manual API configurations must preserve their explicit profile by default");
}
}
int main() {
    try {modes_and_patterns();snr_planning();shannon_capacity();receive_target_lists();automatic_pattern_rates();bandwidth_derived_clocks();explicit_carrier_planning();audio_passband_pattern_roundtrips();physical_simulation_presets();sizing_and_validation();std::cout<<"tuning tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"tuning tests failed: "<<error.what()<<'\n';return 1;}
}
