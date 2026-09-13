#include "datapump/tuning.hpp"
#include "datapump/transfer.hpp"
#include "../src/constellation.hpp"
#include "../src/spreading_code.hpp"
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
    const auto code=modem::detail::spreading_code(config);
    check(std::find(code.begin(),code.end(),1)!=code.end() && std::find(code.begin(),code.end(),-1)!=code.end(),
          "waveform fixtures require measurable chip phase shifts");
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
        changing_pattern(plan.config);
        const Bytes bits{0,1,1,0};
        const auto wave=modem::modulate_status(bits,plan.config);
        check(modem::detect_status(wave,bits,plan.config)>.999,"changing patterns preserve positive and negative status symbols");
    }
    rejects([]{tuning::parse_pattern_mode("pattern-7");},"reject unlisted pattern");
    rejects([]{tuning::parse_pattern_mode("tone-64");},"reject unlisted tone");
    auto pattern=tuning::resolve(24000,100,tuning::PatternMode::pattern_8,false).config;
    check(!pattern.scramble,"forced plaintext patterns do not turn on keystream");
    changing_pattern(pattern);
    for(const auto mode:{tuning::PatternMode::pattern_3,tuning::PatternMode::pattern_16}) {
        auto config=tuning::resolve(24000,100,mode,false).config;
        changing_pattern(config);
        auto preamble=modem::preamble(config),wire=preamble;
        wire.insert(wire.end(),{0,0xff,0x35,0xa8});
        check(modem::demodulate(modem::modulate(wire,config),config,preamble).bytes==wire,"changing-pattern packet roundtrip");
    }
}
void snr_planning() {
    auto plan=tuning::resolve(1200,6,tuning::PatternMode::auto_pattern,false);
    check(plan.config.constellation_bits<=3,"weak links choose a sparse efficient constellation");
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
        check(modem::training_sample_count(plan.config)==5ULL*plan.config.sample_rate,"carrier changes must preserve the fixed training duration");
        near(modem::bit_rate(plan.config),3*bandwidth,"strong channel throughput scales without an audio-rate ceiling");
        const auto budget=tuning::link_budget(tuning::simulation_presets()[1],bandwidth,plan.config.sample_rate);
        check(std::isfinite(budget.sample_snr_db),"low and SDR-rate clocks need valid link budgets");
    }
    const auto config=tuning::resolve(100,100,tuning::PatternMode::pattern_3,false).config;
    changing_pattern(config);
    const auto training=modem::preamble(config);
    auto wire=training;wire.insert(wire.end(),{0,0xff,0x35,0xa8});
    check(modem::demodulate(modem::modulate(wire,config),config,training).bytes==wire,"narrow audio passband preserves packet symbols");
    const modem::Config defaults;
    near(defaults.carrier_hz,tuning::recommended_carrier_hz(defaults.bandwidth_hz),"raw default carrier differs from the automatic carrier");
    check(defaults.sample_rate==tuning::recommended_sample_rate(defaults.bandwidth_hz),"raw default clock differs from the automatic clock");
}
void audio_passband_packet_roundtrips() {
    // These exercise actual real PCM, including non-integer carrier cycles
    // per chip and the short symbols chosen for a strong 64-APSK channel.
    // Integrated simulation alone cannot expose I/Q bin boundary errors.
    const std::pair<double,tuning::PatternMode> cases[]{
        {100,tuning::PatternMode::pattern_3},{1200,tuning::PatternMode::pattern_8},
        {1200,tuning::PatternMode::pattern_3},{1499,tuning::PatternMode::pattern_3},
        {1499.25,tuning::PatternMode::pattern_3},
        {1703,tuning::PatternMode::pattern_3},{1800,tuning::PatternMode::pattern_3}};
    Message message;message.id[0]=17;
    for(unsigned i=0;i<128;++i)message.data.push_back(static_cast<std::uint8_t>(i));
    for(const auto& [bandwidth,mode]:cases) {
        transfer::Options options;
        options.modem=tuning::resolve(bandwidth,100,mode,false).config;
        changing_pattern(options.modem);
        check(options.modem.constellation_bits==6,"strong audio fixture did not choose a dense constellation");
        const auto samples=transfer::transmit(message,options);
        try {
            const auto decoded=transfer::receive(samples,options);
            check(decoded.packet.message.data==message.data,"audio passband corrupted the packet payload");
        } catch(const std::exception& error) {
            throw std::runtime_error("audio passband "+std::to_string(bandwidth)+" Hz "+
                std::string(tuning::pattern_mode_name(mode))+": "+error.what());
        }
    }
}
void adaptive_geometry_and_rates() {
    for(unsigned bits=2;bits<=6;++bits) {
        const auto points=1U<<bits;double energy=0;
        std::vector<std::complex<double>> constellation;
        for(unsigned value=0;value<points;++value) {
            const auto point=modem::detail::mapped(value,bits,{1,0});energy+=std::norm(point);constellation.push_back(point);
            // A seven-degree inter-symbol phase drift remains safely within
            // every supported phase cell, including the dense amplitude rings.
            const auto drifted=point*std::polar(1.,7*std::numbers::pi/180);
            check(modem::detail::decision(drifted,{1,0},1,bits)==value,"phase drift changed a symbol decision");
        }
        for(unsigned ring=1;ring<modem::detail::rings(bits);++ring) {
            const auto previous=modem::detail::decision({modem::detail::radius_step(bits)*ring,0},{1,0},1,bits);
            const auto next=modem::detail::decision({modem::detail::radius_step(bits)*(ring+1),0},{1,0},1,bits);
            check(std::popcount(previous^next)==1,"adjacent amplitude rings need Gray coding");
        }
        near(energy/points,2*modem::nominal_signal_power,"adaptive profiles preserve common transmitted average power");
        check(modem::detail::rings(bits)>=2 && (1U<<modem::detail::phase_bits(bits))<=8,"all profiles use amplitude and bounded phase density");
        for(std::size_t i=0;i<constellation.size();++i)for(std::size_t j=i+1;j<constellation.size();++j)
            check(std::abs(constellation[i]-constellation[j])>.01,"constellation contains overlapping points");
        modem::Config config;config.constellation_bits=bits;
        check(modem::payload_symbol_count(1,config)>=2,"one byte collapsed to one symbol");
    }
    // Exercise the planning margin independently of packet framing and gain
    // fitting. Both differential observations contain independent AWGN, and
    // each symbol includes the explicitly budgeted phase drift.
    std::mt19937_64 random(7219);
    for(unsigned bits=2;bits<=6;++bits) {
        const auto ratio=std::pow(10.,tuning::constellation_target_symbol_snr_db(bits)/10);
        std::normal_distribution<double> noise(0,std::sqrt(.30625/(2*ratio)));
        unsigned errors=0;
        for(unsigned i=0;i<30000;++i) {
            const auto prior=modem::detail::mapped(static_cast<unsigned>(random()%(1U<<bits)),bits,{1,0});
            const auto value=static_cast<unsigned>(random()%(1U<<bits));
            const auto point=modem::detail::mapped(value,bits,prior)*std::polar(1.,std::numbers::pi/64);
            const auto received=point+std::complex<double>{noise(random),noise(random)};
            const auto preceding=prior+std::complex<double>{noise(random),noise(random)};
            if(modem::detail::decision(received,preceding,1,bits)!=value)++errors;
        }
        check(errors<300,"geometry margin exceeds one-percent symbol errors in seeded AWGN/drift test");
    }
    const auto fast=tuning::resolve(2400,100,tuning::PatternMode::auto_pattern,false);
    check(fast.config.constellation_bits==6 && fast.config.spreading_factor==1,"strong links use the densest bounded constellation at full symbol rate");
    near(modem::bit_rate(fast.config),7200,"high-C/N0 adaptive gross throughput");
    const auto weak=tuning::resolve(2400,-20,tuning::PatternMode::auto_pattern,false);
    const auto weaker=tuning::resolve(2400,-30,tuning::PatternMode::auto_pattern,false);
    check(weak.config.constellation_bits<=3,"weak links favor useful rate over dense slow symbols");
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
    clock.constellation_bits=7;rejects([&]{modem::validate(clock);},"unbounded phase/amplitude density accepted");
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
    auto config=tuning::resolve(24000,100,tuning::PatternMode::pattern_3,false).config;
    changing_pattern(config);
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
}
}
int main() {
    try {modes_and_patterns();snr_planning();adaptive_geometry_and_rates();bandwidth_derived_clocks();audio_passband_packet_roundtrips();physical_simulation_presets();sizing_and_validation();std::cout<<"tuning tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"tuning tests failed: "<<error.what()<<'\n';return 1;}
}
