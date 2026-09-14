#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
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
constexpr std::array<unsigned,9> automatic_lengths{64,128,256,512,1024,2048,4096,8192,16384};
constexpr double pattern_target_symbol_snr_db=18;
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
bool tone_mode(PatternMode mode){return mode==PatternMode::auto_tone || index_of(mode)>=9;}
PatternMode parse_pattern_mode(std::string_view name) {
    const auto found=std::find(names.begin(),names.end(),name);
    if(found==names.end()) throw Error("unknown pattern mode: "+std::string(name));
    return modes[static_cast<std::size_t>(found-names.begin())];
}
ReceiveTargets parse_receive_targets(std::string_view text) {
    const auto invalid=[] {ReceiveTargets result;result.reset=true;return result;};
    if(text.empty() || text.size()>maximum_receive_target_text)return invalid();
    ReceiveTargets result;result.values.clear();result.canonical.clear();
    std::size_t entries=0;
    while(true) {
        if(++entries>maximum_receive_targets)return invalid();
        const auto comma=text.find(',');
        auto token=text.substr(0,comma);
        while(!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))token.remove_prefix(1);
        while(!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))token.remove_suffix(1);
        // Floating from_chars accepts a leading minus but no leading plus.
        if(!token.empty() && token.front()=='+') {
            token.remove_prefix(1);
            if(!token.empty() && (token.front()=='+' || token.front()=='-'))return invalid();
        }
        if(token.empty())return invalid();
        double value=0;
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=token.data()+token.size() ||
           !std::isfinite(value) || value < -200 || value > 200)return invalid();
        if(value==0)value=0; // Canonicalize negative zero.
        if(std::find(result.values.begin(),result.values.end(),value)==result.values.end())result.values.push_back(value);
        if(comma==std::string_view::npos)break;
        text.remove_prefix(comma+1);
    }
    for(const auto value:result.values) {
        std::array<char,64> buffer{};
        const auto formatted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value);
        if(formatted.ec!=std::errc{})return invalid();
        if(!result.canonical.empty())result.canonical+=", ";
        result.canonical.append(buffer.data(),formatted.ptr);
    }
    return result;
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
    Plan plan;
    auto& base=plan.config;
    base.pattern_symbols=true;
    base.constellation_bits=1;
    base.bandwidth_hz=bandwidth_hz;
    // Real passband PCM must sample the carrier even when the message band is
    // very narrow. Audio endpoints negotiate their hardware clock separately.
    base.sample_rate=sample_rate;
    base.carrier_hz=recommended_carrier_hz(bandwidth_hz);
    const bool tone=tone_mode(mode);
    base.spreading_mode=tone?modem::SpreadingMode::tone:modem::SpreadingMode::pattern;
    // The pattern itself must identify a keyed signal. Public templates with
    // only a Data mask give every receive key the same acquisition evidence.
    base.scramble=!tone && encryption;
    const double chip_seconds=2/bandwidth_hz;
    plan.target_symbol_snr_db=pattern_target_symbol_snr_db;
    const double exponent=(plan.target_symbol_snr_db-target_snr_db_hz)/10-std::log10(chip_seconds);
    plan.required_spreading=exponent>std::log10(std::numeric_limits<double>::max())?
        std::numeric_limits<double>::infinity():std::max(1.,std::pow(10.,exponent));
    if(lengths[index])base.spreading_factor=lengths[index];
    else {
        const auto found=std::lower_bound(automatic_lengths.begin(),automatic_lengths.end(),plan.required_spreading);
        base.spreading_factor=found==automatic_lengths.end()?automatic_lengths.back():*found;
        if(found==automatic_lengths.end()) {
            base.integration_seconds=std::pow(10.,(plan.target_symbol_snr_db-target_snr_db_hz)/10);
            if(!std::isfinite(base.integration_seconds))throw Error("requested integration exceeds numeric duration range");
        }
    }
    modem::validate(base);
    const double seconds=modem::symbol_seconds(base);
    plan.estimated_processing_gain_db=10*std::log10(seconds/chip_seconds);
    plan.estimated_symbol_snr_db=target_snr_db_hz+10*std::log10(seconds);
    plan.target_supported=base.spreading_factor>=automatic_lengths.front() &&
        plan.estimated_symbol_snr_db+1e-10>=plan.target_symbol_snr_db;
    std::ostringstream explanation;
    explanation<<std::fixed<<std::setprecision(1)<<"Two sparse pattern symbols (1 raw bit/symbol), "
        <<base.spreading_factor<<" nominal chips and "<<seconds
        <<" seconds/symbol; modeled Es/N0 "<<plan.estimated_symbol_snr_db<<" dB, target "<<plan.target_symbol_snr_db<<" dB. "
        <<"Automatic selection reserves at least 64 chips for pattern evidence. ";
    if(!plan.target_supported)explanation<<"The forced length is preserved but does not meet the standalone pattern confidence target. ";
    if(tone)explanation<<"Tone modes are unencrypted and do not provide Low-Probability-of-Intercept protection. ";
    if(mode==PatternMode::auto_keystream && !encryption)explanation<<"Without a key, auto-pattern is used. ";
    explanation<<"The 18 dB integration target is an initial model, not calibrated detection sensitivity or a false-alarm guarantee. Acquisition uses received pattern evidence. Both endpoints derive the profile from matching bandwidth, C/N0 and pattern settings.";
    plan.explanation=explanation.str();
    return plan;
}
std::vector<modem::Config> receive_profiles(double bandwidth_hz,std::span<const double> targets_db_hz,
                                           PatternMode mode,bool encryption) {
    modem::Config base;base.bandwidth_hz=bandwidth_hz;
    base.sample_rate=recommended_sample_rate(bandwidth_hz);base.carrier_hz=recommended_carrier_hz(bandwidth_hz);
    return receive_profiles(base,targets_db_hz,mode,encryption);
}
std::vector<modem::Config> receive_profiles(const modem::Config& base,std::span<const double> targets_db_hz,
                                           PatternMode mode,bool encryption) {
    if(targets_db_hz.empty() || targets_db_hz.size()>maximum_receive_targets)throw Error("receive target list must contain 1..16 entries");
    std::vector<modem::Config> profiles;
    for(const auto target:targets_db_hz) {
        if(!std::isfinite(target) || target < -200 || target > 200)throw Error("receive target must be finite -200..200 dB-Hz");
        const auto plan=resolve(base.bandwidth_hz,target,mode,encryption).config;
        auto config=base;
        config.pattern_symbols=plan.pattern_symbols;config.constellation_bits=plan.constellation_bits;
        config.spreading_factor=plan.spreading_factor;config.integration_seconds=plan.integration_seconds;
        config.spreading_mode=plan.spreading_mode;config.scramble=plan.scramble;
        if(config.spreading_mode==modem::SpreadingMode::tone) {
            config.dsss=false;config.data_key.reset();
            config.spreading_seed.fill(0);config.dsss_seed.fill(0);
        }
        modem::validate(config);
        const auto duplicate=std::any_of(profiles.begin(),profiles.end(),[&](const auto& prior) {
            return prior.sample_rate==config.sample_rate && prior.carrier_hz==config.carrier_hz &&
                prior.bandwidth_hz==config.bandwidth_hz && prior.constellation_bits==config.constellation_bits &&
                prior.pattern_symbols==config.pattern_symbols && prior.spreading_factor==config.spreading_factor &&
                modem::symbol_sample_count(prior)==modem::symbol_sample_count(config) && prior.spreading_mode==config.spreading_mode &&
                prior.scramble==config.scramble && prior.dsss==config.dsss;
        });
        if(!duplicate)profiles.push_back(std::move(config));
    }
    return profiles;
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
