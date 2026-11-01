// Pure public preset registry shared by the ordinary application and isolated studies.
#include "datapump/tuning.hpp"
#include <array>
#include <string>

namespace datapump::tuning {
namespace {
constexpr std::array oscillator_models{
    OscillatorPreset{"crystal","Free-running crystal",100,.5},
    // Relative worst-case sum of two independent +/-20 ppm crystal radios.
    OscillatorPreset{"cb-ssb","CB SSB / ATS Mini V4 SSB: +/-20 ppm/radio",40,.5,false},
    // Icom specifies each radio at +/-0.5 ppm (430 MHz, 0..50 C).
    // Relative worst-case sum is 1 ppm; diffusion is an illustrative assumption.
    OscillatorPreset{"ic-7100","IC-7100: +/-0.5 ppm/radio",1,.05,false},
    // Shared locked-link residual assumption; oscillator class changes the
    // phase-diffusion stress model, not an inferred unlocked frequency budget.
    OscillatorPreset{"gpsdo-xo","GPSDO: hobbyist XO (no oven)",.0001,.5},
    OscillatorPreset{"gpsdo-tcxo","GPSDO: TCXO (no oven)",.0001,.05},
    OscillatorPreset{"gpsdo-ocxo","GPSDO: OCXO",.0001,.005}};
constexpr std::array spread_models{
    SpreadPreset{"none","None — 0 Hz 0 ms",0,0,false},
    SpreadPreset{"sub9-best","Sub9 best 0.001Hz 0.005ms",.005,.001,false},
    SpreadPreset{"sub9-moderate","Sub9 moderate 0.005Hz 0.05ms",.05,.005,false},
    SpreadPreset{"sub9-worst","Sub9 worst 0.05Hz 5ms",5,.05,false},
    SpreadPreset{"hf-best","HF best 0.1Hz 0.5ms",.5,.1,true},
    SpreadPreset{"hf-moderate","HF moderate 10Hz 6ms",6,10,true},
    SpreadPreset{"hf-worst","HF worst 30Hz 7ms",7,30,true},
    SpreadPreset{"aurora-best","Aurora best 10Hz 3ms",3,10,true},
    SpreadPreset{"aurora-worst","Aurora worst 55Hz 11ms",11,55,true},
    SpreadPreset{"eme-focused","EME focused 6Hz 0.5ms",.5,6,true}};
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
std::span<const OscillatorPreset> oscillator_presets(){return oscillator_models;}
OscillatorPreset parse_oscillator_preset(std::string_view name) {
    const auto wanted=normalized(name);
    // Preserve launch commands exported before the equivalent RF models were merged.
    if(wanted==normalized("ats-mini-v4-ssb") ||
       wanted==normalized("ATS Mini V4 SSB crystal: +/-20 ppm/radio") ||
       wanted==normalized("Modern CB SSB crystal: +/-20 ppm/radio"))return oscillator_models[1];
    for(const auto& preset:oscillator_models)
        if(normalized(preset.id)==wanted || normalized(preset.name)==wanted)return preset;
    throw Error("unknown oscillator preset: "+std::string(name));
}
std::span<const SpreadPreset> spread_presets(){return spread_models;}
SpreadPreset parse_spread_preset(std::string_view name) {
    const auto wanted=normalized(name);
    for(const auto& preset:spread_models)
        if(normalized(preset.id)==wanted || normalized(preset.name)==wanted)return preset;
    throw Error("unknown spread preset: "+std::string(name));
}
} // namespace datapump::tuning
