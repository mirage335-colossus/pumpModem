#include "datapump/tuning.hpp"
#include "constellation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace datapump::tuning {
namespace {
constexpr std::array modes{
    PatternMode::auto_keystream,PatternMode::auto_pattern,PatternMode::auto_tone,
    PatternMode::pattern_3,PatternMode::pattern_4,PatternMode::pattern_6,PatternMode::pattern_8,PatternMode::pattern_12,PatternMode::pattern_16,
    PatternMode::tone_1,PatternMode::tone_2,PatternMode::tone_3,PatternMode::tone_4,PatternMode::tone_8,PatternMode::tone_32,
    PatternMode::tone_128,PatternMode::tone_1024,PatternMode::tone_4096,PatternMode::tone_16384};
constexpr std::array<std::string_view,19> names{
    "auto-keystream","auto-pattern","auto-tone","pattern-3","pattern-4","pattern-6","pattern-8","pattern-12","pattern-16",
    "tone-1","tone-2","tone-3","tone-4","tone-8","tone-32","tone-128","tone-1024","tone-4096","tone-16384"};
constexpr std::array<unsigned,19> lengths{0,0,0,3,4,6,8,12,16,1,2,3,4,8,32,128,1024,4096,16384};
constexpr std::array<unsigned,18> automatic_lengths{1,2,3,4,6,8,12,16,32,64,128,256,512,1024,2048,4096,8192,16384};
constexpr std::array presets{
    SimulationPreset{"no",false,0,0},SimulationPreset{"3dBm -6dB",true,3,-6},
    SimulationPreset{"3dBm -60dB",true,3,-60},SimulationPreset{"3dBm -90dB",true,3,-90},
    SimulationPreset{"3dBm -120dB",true,3,-120},SimulationPreset{"3dBm -170dB",true,3,-170},
    SimulationPreset{"3dBm -200dB",true,3,-200},SimulationPreset{"3dBm -230dB",true,3,-230},
    SimulationPreset{"50dBm -200dB",true,50,-200},SimulationPreset{"50dBm -270dB",true,50,-270},
    SimulationPreset{"70dBm -250dB",true,70,-250}};
std::size_t index_of(PatternMode mode) {
    const auto found=std::find(modes.begin(),modes.end(),mode);
    if(found==modes.end()) throw Error("unknown pattern mode");
    return static_cast<std::size_t>(found-modes.begin());
}
std::string normalized(std::string_view name) {
    std::string result;
    for(auto character:name) {
        if(character==' ' || character=='\t') continue;
        if(character>='A' && character<='Z') character=static_cast<char>(character-'A'+'a');
        result+=character;
    }
    return result;
}
}
std::span<const PatternMode> pattern_modes(){return modes;}
std::string_view pattern_mode_name(PatternMode mode){return names[index_of(mode)];}
PatternMode parse_pattern_mode(std::string_view name) {
    const auto found=std::find(names.begin(),names.end(),name);
    if(found==names.end()) throw Error("unknown pattern mode: "+std::string(name));
    return modes[static_cast<std::size_t>(found-names.begin())];
}
double constellation_target_symbol_snr_db(unsigned bits) {
    if(bits<2 || bits>6)throw Error("constellation must carry 2..6 bits per symbol");
    const double spacing=modem::detail::radius_step(bits);
    const double phases=1U<<modem::detail::phase_bits(bits);
    // Use the worst inner-ring angular separation, including the extra noise
    // of differential phase detection, and reserve 2.8125 degrees for phase
    // drift per symbol. A 3.2-sigma half-distance is an engineering design
    // margin, not a claim of calibrated packet error rate.
    const double angular=std::sqrt(2.)*spacing*std::sin(std::numbers::pi/phases-std::numbers::pi/64);
    const double distance=std::min(spacing,angular);
    return 10*std::log10(2*3.2*3.2*.30625/(distance*distance));
}
std::uint32_t recommended_sample_rate(double bandwidth_hz) {
    const auto carrier=recommended_carrier_hz(bandwidth_hz);
    return static_cast<std::uint32_t>(std::ceil(std::max(4*bandwidth_hz,4*carrier)));
}
double recommended_carrier_hz(double bandwidth_hz) {
    if(!std::isfinite(bandwidth_hz) || bandwidth_hz<1 || bandwidth_hz>maximum_bandwidth_hz)
        throw Error("modem bandwidth must be 1..30000000 Hz");
    return std::max(1500.,.75*bandwidth_hz);
}
Plan resolve(double bandwidth_hz,double target_snr_db_hz,PatternMode mode,bool encryption) {
    const auto sample_rate=recommended_sample_rate(bandwidth_hz);
    if(!std::isfinite(target_snr_db_hz)) throw Error("target C/N0 must be finite dB-Hz");
    const auto index=index_of(mode);
    modem::Config base;
    base.bandwidth_hz=bandwidth_hz;
    // Real passband PCM must sample the carrier even when the message band is
    // very narrow. Audio endpoints negotiate their hardware clock separately.
    base.sample_rate=sample_rate;
    base.carrier_hz=recommended_carrier_hz(bandwidth_hz);
    const bool tone=mode==PatternMode::auto_tone || index>=9;
    base.spreading_mode=tone?modem::SpreadingMode::tone:modem::SpreadingMode::pattern;
    base.scramble=mode==PatternMode::auto_keystream && encryption;
    const double chip_seconds=2/bandwidth_hz;
    Plan plan;double best_rate=-1;bool any=false;
    for(unsigned bits=2;bits<=6;++bits) {
        Plan candidate;candidate.config=base;candidate.config.constellation_bits=bits;
        candidate.target_symbol_snr_db=constellation_target_symbol_snr_db(bits);
        const double exponent=(candidate.target_symbol_snr_db-target_snr_db_hz)/10-std::log10(chip_seconds);
        candidate.required_spreading=exponent>std::log10(std::numeric_limits<double>::max())?
            std::numeric_limits<double>::infinity():std::max(1.,std::pow(10.,exponent));
        if(lengths[index])candidate.config.spreading_factor=lengths[index];
        else {
            const auto found=std::lower_bound(automatic_lengths.begin(),automatic_lengths.end(),candidate.required_spreading);
            candidate.config.spreading_factor=found==automatic_lengths.end()?automatic_lengths.back():*found;
            if(found==automatic_lengths.end()) {
                candidate.config.integration_seconds=std::pow(10.,(candidate.target_symbol_snr_db-target_snr_db_hz)/10);
                if(!std::isfinite(candidate.config.integration_seconds))continue;
            }
        }
        try{modem::validate(candidate.config);}catch(const Error&){continue;}
        const double seconds=modem::symbol_seconds(candidate.config);
        candidate.estimated_processing_gain_db=10*std::log10(seconds/chip_seconds);
        candidate.estimated_symbol_snr_db=target_snr_db_hz+10*std::log10(seconds);
        candidate.target_supported=candidate.estimated_symbol_snr_db+1e-10>=candidate.target_symbol_snr_db;
        const auto rate=modem::bit_rate(candidate.config);
        // Meet the margin first; among supported profiles maximize gross bit
        // rate. For an impossible forced duration retain the most robust one.
        if(!any || (candidate.target_supported && !plan.target_supported) ||
           (candidate.target_supported==plan.target_supported &&
             (candidate.target_supported?rate>best_rate:candidate.target_symbol_snr_db<plan.target_symbol_snr_db))) {
            plan=std::move(candidate);best_rate=rate;any=true;
        }
    }
    if(!any)throw Error("requested integration exceeds numeric duration range");
    std::ostringstream explanation;
    explanation<<std::fixed<<std::setprecision(1)<<(1U<<plan.config.constellation_bits)<<"APSK ("
        <<plan.config.constellation_bits<<" bits/symbol, "<<(1U<<modem::detail::phase_bits(plan.config.constellation_bits))
        <<" phase positions, "<<modem::detail::rings(plan.config.constellation_bits)<<" amplitude rings): "
        <<"maximum modeled throughput among supported profiles with a geometric noise/drift margin. "
        <<plan.config.spreading_factor<<" template chips, "<<modem::symbol_seconds(plan.config)
        <<" seconds/symbol, estimated Es/N0 "<<plan.estimated_symbol_snr_db<<" dB; target "<<plan.target_symbol_snr_db<<" dB. ";
    if(!plan.target_supported)explanation<<"The selected forced duration does not meet this target. ";
    if(mode==PatternMode::auto_keystream && !encryption)explanation<<"Without a key, auto-pattern is used. ";
    explanation<<"Both endpoints derive the profile from matching bandwidth, C/N0 and pattern settings. These margins are engineering estimates, not measured decoder sensitivity.";
    plan.explanation=explanation.str();
    return plan;
}
std::span<const SimulationPreset> simulation_presets(){return presets;}
SimulationPreset parse_simulation_preset(std::string_view name) {
    const auto wanted=normalized(name);
    if(wanted=="off") return presets[0];
    for(const auto& preset:presets) if(normalized(preset.name)==wanted) return preset;
    throw Error("unknown simulation preset: "+std::string(name));
}
LinkBudget link_budget(const SimulationPreset& preset,double bandwidth_hz,std::uint32_t sample_rate,double noise_figure_db) {
    if(!preset.enabled) throw Error("simulation preset is disabled");
    if(!std::isfinite(bandwidth_hz) || bandwidth_hz<=0 || bandwidth_hz>maximum_bandwidth_hz || sample_rate<64 || sample_rate>120000000 ||
       bandwidth_hz>static_cast<double>(sample_rate)/2 || !std::isfinite(noise_figure_db) || noise_figure_db<0 ||
       !std::isfinite(preset.transmit_dbm) || !std::isfinite(preset.attenuation_db) || preset.attenuation_db>0)
        throw Error("invalid simulation link budget");
    constexpr double thermal_dbm_hz=-174;
    const double density=thermal_dbm_hz+noise_figure_db;
    LinkBudget result;
    result.received_power_dbm=preset.transmit_dbm+preset.attenuation_db;
    result.noise_power_dbm=density+10*std::log10(bandwidth_hz);
    result.snr_db_hz=result.received_power_dbm-density;
    result.snr_db=result.received_power_dbm-result.noise_power_dbm;
    result.sample_snr_db=result.snr_db_hz-10*std::log10(static_cast<double>(sample_rate)/2);
    return result;
}
}
