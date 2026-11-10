#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <utility>

namespace datapump::tuning {
namespace {
constexpr std::array presets{
    SimulationPreset{"no",false,0,0},SimulationPreset{"3dBm -6dB",true,3,-6},
    SimulationPreset{"3dBm -60dB",true,3,-60},SimulationPreset{"3dBm -90dB",true,3,-90},
    SimulationPreset{"3dBm -120dB",true,3,-120},SimulationPreset{"3dBm -170dB",true,3,-170},
    SimulationPreset{"3dBm -200dB",true,3,-200},SimulationPreset{"3dBm -230dB",true,3,-230},
    SimulationPreset{"50dBm -200dB",true,50,-200},SimulationPreset{"50dBm -270dB",true,50,-270},
    SimulationPreset{"70dBm -250dB",true,70,-250}};
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
modem::OscillatorModel oscillator_model(const OscillatorPreset& preset) {
    return {preset.clock_error_ppm,preset.phase_noise_degrees_per_sqrt_second};
}
Doppler parse_doppler(std::string_view text) {
    const auto invalid=[] {throw Error("doppler must be a finite velocity in c, knots, mph or kph, or a frequency shift in %; velocity must be below light speed");};
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.front())))text.remove_prefix(1);
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.back())))text.remove_suffix(1);
    if(text.empty()||text.size()>128)invalid();
    if(text.front()=='+') {
        text.remove_prefix(1);
        if(text.empty()||text.front()=='+'||text.front()=='-')invalid();
    }
    double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr==text.data()||!std::isfinite(value))invalid();
    auto suffix=std::string_view(parsed.ptr,static_cast<std::size_t>(text.data()+text.size()-parsed.ptr));
    while(!suffix.empty()&&std::isspace(static_cast<unsigned char>(suffix.front())))suffix.remove_prefix(1);
    std::string unit(suffix);
    for(auto& character:unit)if(character>='A'&&character<='Z')character=static_cast<char>(character-'A'+'a');
    constexpr long double light_speed=299792458.L;
    long double beta=0;
    if(unit=="c")beta=value;
    else if(unit=="knots")beta=static_cast<long double>(value)*(1852.L/3600)/light_speed;
    else if(unit=="mph")beta=static_cast<long double>(value)*(1609.344L/3600)/light_speed;
    else if(unit=="kph")beta=static_cast<long double>(value)*(1000.L/3600)/light_speed;
    else if(unit=="%") {
        if(value<=-100)invalid();
        const auto ratio=1+static_cast<long double>(value)/100;
        // The reciprocal form avoids squaring an arbitrarily large ratio.
        const auto squared=ratio>1?1/(ratio*ratio):ratio*ratio;
        beta=ratio>1?(squared-1)/(squared+1):(1-squared)/(1+squared);
    } else invalid();
    Doppler result;
    result.velocity_c=static_cast<double>(beta);
    if(!std::isfinite(result.velocity_c)||std::abs(result.velocity_c)>=1)invalid();
    if(result.velocity_c==0)result.velocity_c=0;
    // Use the canonical velocity for every derived value, including percentage
    // imports, so launch export/import cannot change the calculated carrier.
    beta=result.velocity_c;
    const auto ratio=std::sqrt((1-beta)/(1+beta));
    result.frequency_ratio=static_cast<double>(ratio);
    result.frequency_shift_percent=static_cast<double>((ratio-1)*100);
    if(!std::isfinite(result.frequency_ratio)||result.frequency_ratio<=0)invalid();
    if(result.velocity_c!=0) {
        std::array<char,64> buffer{};
        const auto formatted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),result.velocity_c,
            std::chars_format::general,std::numeric_limits<double>::max_digits10);
        if(formatted.ec!=std::errc{})invalid();
        result.canonical.assign(buffer.data(),formatted.ptr);result.canonical+='c';
    }
    return result;
}
double doppler_carrier_hz(double configured_physical_carrier_hz,const Doppler& doppler) {
    if(!std::isfinite(configured_physical_carrier_hz)||configured_physical_carrier_hz<=0||
       !std::isfinite(doppler.frequency_ratio)||doppler.frequency_ratio<=0)
        throw Error("Doppler-adjusted physical carrier must be finite and positive");
    const auto adjusted=static_cast<long double>(configured_physical_carrier_hz)*doppler.frequency_ratio;
    if(!std::isfinite(adjusted)||adjusted>std::numeric_limits<double>::max()||
       static_cast<double>(adjusted)<=0)
        throw Error("Doppler-adjusted physical carrier must be finite and positive");
    return static_cast<double>(adjusted);
}
std::uint32_t recommended_sample_rate(double bandwidth_hz,std::optional<double> carrier_hz) {
    const auto automatic_carrier=recommended_carrier_hz(bandwidth_hz);
    const auto carrier=carrier_hz.value_or(automatic_carrier);
    if(!std::isfinite(carrier) || carrier<=0 || carrier>30000000)
        throw Error("modem carrier must be finite, positive and at most 30000000 Hz");
    const auto minimum=static_cast<std::uint32_t>(std::ceil(std::max({64.,4*bandwidth_hz,4*carrier})));
    return minimum;
}
double recommended_carrier_hz(double bandwidth_hz) {
    if(!std::isfinite(bandwidth_hz) || bandwidth_hz<minimum_bandwidth_hz || bandwidth_hz>maximum_bandwidth_hz)
        throw Error("modem bandwidth must be 0.001..30000000 Hz");
    return std::max(1500.,.75*bandwidth_hz);
}
double shannon_capacity_bps(double bandwidth_hz,double target_snr_db_hz) {
    if(!std::isfinite(bandwidth_hz) || bandwidth_hz<minimum_bandwidth_hz || bandwidth_hz>maximum_bandwidth_hz)
        throw Error("modem bandwidth must be 0.001..30000000 Hz");
    if(!std::isfinite(target_snr_db_hz))throw Error("target C/N0 must be finite dB-Hz");
    const auto log_snr=target_snr_db_hz*(std::numbers::ln10/10)-std::log(bandwidth_hz);
    // log1p preserves weak-signal capacity; the positive branch avoids
    // overflowing the linear SNR when the finite dB-Hz target is very large.
    const auto capacity_per_hz=log_snr>0?
        log_snr+std::log1p(std::exp(-log_snr)):std::log1p(std::exp(log_snr));
    return (bandwidth_hz/std::numbers::ln2)*capacity_per_hz;
}
// Preserve validation/round trips for the visible saved preference. These
// values cannot select a receiver or waveform while Robust is unavailable.
modem::OuterDsssVersion parse_outer_dsss_version(std::string_view name) {
    if(name=="legacy")return modem::OuterDsssVersion::legacy_v1;
    if(name=="interleave"||name=="interleaved-v2")return modem::OuterDsssVersion::interleaved_v2;
    throw Error("dsss-version must be interleave or legacy");
}
std::string_view outer_dsss_version_id(modem::OuterDsssVersion version) {
    switch(version) {
    case modem::OuterDsssVersion::legacy_v1:return "legacy";
    case modem::OuterDsssVersion::interleaved_v2:return "interleave";
    }
    throw Error("invalid saved DSSS preference");
}
modem::SearchArithmetic parse_search_arithmetic(std::string_view name) {
    using A=modem::SearchArithmetic;
    if(name=="default"||name=="automatic")return A::default_mode;
    if(name=="matrix4"||name=="4-bit")
        throw Error("4-bit matrix search arithmetic is not implemented");
    // Old integer selections migrate to the supported automatic policy. The
    // explicit enum/kernel diagnostics remain available to internal experiments.
    if(name=="matrix8"||name=="8-bit"||name=="int8"||name=="INT8")return A::default_mode;
    if(name=="fp32-min"||name=="fp32"||name=="FP32")return A::fp32;
    if(name=="fp64-force"||name=="fp64"||name=="FP64")return A::fp64;
    throw Error("search-arithmetic must be default, fp32-min or fp64-force (legacy integer identifiers select default; matrix4 is unavailable)");
}
std::string_view search_arithmetic_id(modem::SearchArithmetic arithmetic) {
    using A=modem::SearchArithmetic;
    switch(arithmetic) {
    case A::default_mode:return "default";
    case A::matrix4:return "matrix4";
    case A::matrix8:return "matrix8";
    case A::int8:return "int8";
    case A::fp32:return "fp32-min";
    case A::fp64:return "fp64-force";
    }
    throw Error("invalid receiver search arithmetic");
}
modem::ReceiverStrategy parse_receiver_strategy(std::string_view name) {
    using S=modem::ReceiverStrategy;
    if(name=="auto"||name=="automatic")return S::automatic;
    if(name=="existing")return S::existing_only;
    if(name=="complex")return S::complex_only;
    if(name=="amplitude")return S::amplitude_only;
    throw Error("receiver-strategy must be auto, existing, complex or amplitude");
}
std::string_view receiver_strategy_id(modem::ReceiverStrategy strategy) {
    using S=modem::ReceiverStrategy;
    switch(strategy) {
    case S::automatic:return "auto";
    case S::existing_only:return "existing";
    case S::complex_only:return "complex";
    case S::amplitude_only:return "amplitude";
    }
    throw Error("invalid receiver strategy");
}
void validate_receiver_cpu_budget(double ratio) {
    if(!std::isfinite(ratio)||ratio<=0)throw Error("receiver CPU budget must be finite and positive");
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
