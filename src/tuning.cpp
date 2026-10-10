#include "datapump/tuning.hpp"
#include "datapump/pattern_code.hpp"
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
constexpr std::array modes{
    PatternMode::auto_keystream,PatternMode::auto_pattern,PatternMode::auto_tone,
    PatternMode::pattern_3,PatternMode::pattern_4,PatternMode::pattern_6,PatternMode::pattern_8,PatternMode::pattern_12,PatternMode::pattern_16,
    PatternMode::tone_1,PatternMode::tone_2,PatternMode::tone_3,PatternMode::tone_4,PatternMode::tone_8,PatternMode::tone_32,
    PatternMode::tone_128,PatternMode::tone_1024,PatternMode::tone_4096,PatternMode::tone_16384};
constexpr std::array<std::string_view,19> names{
    "auto-keystream","auto-pattern","auto-tone","pattern-3","pattern-4","pattern-6","pattern-8","pattern-12","pattern-16",
    "tone-1","tone-2","tone-3","tone-4","tone-8","tone-32","tone-128","tone-1024","tone-4096","tone-16384"};
constexpr std::array<unsigned,19> lengths{0,0,0,3,4,6,8,12,16,1,2,3,4,8,32,128,1024,4096,16384};
constexpr std::array<unsigned,11> automatic_lengths{16,32,64,128,256,512,1024,2048,4096,8192,16384};
unsigned minimum_pattern_chips(const modem::Config& config,double target_snr_db_hz,bool tone) {
    // Outer chips share an inner coefficient. They cannot satisfy the private
    // codeword-length floor; shorter outer profiles lack channel qualification.
    if(config.dsss_factor>1) {
        auto candidate=config;candidate.integration_seconds=0;
        const auto fine=modem::pattern_chip_samples(candidate);
        // Compare actual integer geometry. A ceil() of the inverse bandwidth
        // calculation could turn 64+one rounding ulp into a 128-chip plan.
        for(const auto length:automatic_lengths) {
            candidate.spreading_factor=length;
            if(modem::symbol_sample_count(candidate)/fine/config.dsss_factor>=64)return length;
        }
        throw Error("outer DSSS cannot retain 64 complete inner chips at this sample rate");
    }
    // A high C/N0 is only a fast-link target relative to the selected band.
    // Preserve the previous weak-signal and tone plans. Shorter patterns need
    // substantial in-band headroom as well as integrated energy: clean PCM
    // alone overstates their tolerance of fractional timing and clock error.
    // These conservative engineering floors do not replace RX evidence tests.
    const auto band_snr=target_snr_db_hz-10*std::log10(config.bandwidth_hz);
    unsigned minimum=!tone && band_snr>=30?16:!tone && band_snr>=24?32:64;
    // A partial final chip would introduce a new periodic short-hold cadence
    // when the symbol is shortened. Keep the previous integration floor if
    // neither short profile consists entirely of whole sample-quantized chips.
    const auto chip=static_cast<std::uint64_t>(std::ceil(2.*config.sample_rate/config.bandwidth_hz));
    auto candidate=config;candidate.integration_seconds=0;
    while(minimum<64) {
        candidate.spreading_factor=minimum;
        const auto samples=modem::symbol_sample_count(candidate);
        if(samples%chip==0) {
            // Keep the compact private receive path when its carrier bases
            // are orthogonal. Endpoint/channel probes require 32 chips on
            // that path, even when total signal energy is ample.
            bool compact_private=false;
            if(config.scramble || config.dsss) {
                const auto bin=std::gcd(std::gcd(chip,samples),std::max<std::uint64_t>(1,chip/2));
                const auto omega=2*std::numbers::pi*config.carrier_hz/config.sample_rate;
                const auto sine=std::sin(omega);
                const auto image=std::abs(sine)>1e-12?
                    std::abs(std::sin(static_cast<double>(bin)*omega)/sine):static_cast<double>(bin);
                compact_private=image<=1e-10*static_cast<double>(bin);
            }
            // Only the bounded exact-sample path and the orthogonal private
            // path have short-pattern channel coverage. Preserve the old
            // floor for other long-sample geometries.
            if(samples>256 && !compact_private)return 64;
            if(minimum==16 && compact_private) {minimum=32;continue;}
            break;
        }
        minimum*=2;
    }
    return minimum;
}
constexpr std::array presets{
    SimulationPreset{"no",false,0,0},SimulationPreset{"3dBm -6dB",true,3,-6},
    SimulationPreset{"3dBm -60dB",true,3,-60},SimulationPreset{"3dBm -90dB",true,3,-90},
    SimulationPreset{"3dBm -120dB",true,3,-120},SimulationPreset{"3dBm -170dB",true,3,-170},
    SimulationPreset{"3dBm -200dB",true,3,-200},SimulationPreset{"3dBm -230dB",true,3,-230},
    SimulationPreset{"50dBm -200dB",true,50,-200},SimulationPreset{"50dBm -270dB",true,50,-270},
    SimulationPreset{"70dBm -250dB",true,70,-250}};
constexpr std::array oscillator_models{
    OscillatorPreset{"crystal","Free-running crystal",100,.5},
    // Icom specifies each radio at +/-0.5 ppm (430 MHz, 0..50 C).
    // Relative worst-case sum is 1 ppm; diffusion is an illustrative assumption.
    OscillatorPreset{"ic-7100","IC-7100: +/-0.5 ppm/radio",1,.05},
    // Shared locked-link residual assumption; oscillator class changes the
    // phase-diffusion stress model, not an inferred unlocked frequency budget.
    OscillatorPreset{"gpsdo-xo","GPSDO: hobbyist XO (no oven)",.0001,.5},
    OscillatorPreset{"gpsdo-tcxo","GPSDO: TCXO (no oven)",.0001,.05},
    OscillatorPreset{"gpsdo-ocxo","GPSDO: OCXO",.0001,.005}};
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
modem::OscillatorModel oscillator_model(const OscillatorPreset& preset) {
    return {preset.clock_error_ppm,preset.phase_noise_degrees_per_sqrt_second};
}
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
    // Whole sampled half-chips preserve compact receive projections as the carrier
    // moves. For Rate 100, a 1490 Hz tone therefore uses 6000 Hz, not 5960 Hz:
    // the latter introduces partial chips and can expand the FFT by 20 times.
    // Recognize only exact decimal-rate inputs, and spend at most 5% extra PCM
    // work. Unusual fractional rates retain the existing valid minimum clock.
    for(std::uint64_t denominator=1;denominator<=1000000;denominator*=10) {
        const auto numerator=static_cast<std::uint64_t>(std::round(bandwidth_hz*static_cast<double>(denominator)));
        if(!numerator || static_cast<double>(numerator)/static_cast<double>(denominator)!=bandwidth_hz)continue;
        const auto quantum=numerator/std::gcd(numerator,denominator);
        const auto candidate=(minimum+quantum-1)/quantum*quantum;
        if(candidate>120000000 || candidate>minimum+minimum/20)break;
        const auto chip=std::ceil(2.*static_cast<double>(candidate)/bandwidth_hz);
        bool aligned=std::fmod(chip,2.)==0;
        // Use the actual symbol-rounding arithmetic, including decimal-input
        // representation error. Never claim alignment from the rational alone.
        for(const auto factor:{1u,64u,16384u})
            aligned &= std::ceil(2.L*candidate*factor/bandwidth_hz)==chip*factor;
        if(aligned)return static_cast<std::uint32_t>(candidate);
        break;
    }
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
namespace {
Plan resolve_config(modem::Config config,double target_snr_db_hz,PatternMode mode,bool encryption) {
    // Check clock/rate before minimum_pattern_chips performs sample arithmetic.
    (void)recommended_carrier_hz(config.bandwidth_hz);
    if(config.sample_rate<64 || config.sample_rate>120000000)
        throw Error("internal sample rate must be 64..120000000 Hz");
    if(!std::isfinite(config.carrier_hz) || config.carrier_hz<=0)
        throw Error("modem carrier must be finite and positive");
    if(!std::isfinite(target_snr_db_hz)) throw Error("target C/N0 must be finite dB-Hz");
    const auto index=index_of(mode);
    Plan plan;
    auto& base=plan.config;
    base=std::move(config);
    base.pattern_symbols=true;
    base.constellation_bits=1;
    base.spreading_factor=64;
    base.integration_seconds=0;
    const bool tone=tone_mode(mode);
    base.spreading_mode=tone?modem::SpreadingMode::tone:modem::SpreadingMode::pattern;
    // The pattern itself must identify a keyed signal. Public templates with
    // only a Data mask give every receive key the same acquisition evidence.
    base.scramble=!tone && encryption;
    if(tone) {
        base.dsss=false;base.dsss_factor=1;base.data_key.reset();
        base.outer_dsss_version=modem::OuterDsssVersion::legacy_v1;
        base.spreading_seed.fill(0);base.dsss_seed.fill(0);
    }
    const double chip_seconds=2/base.bandwidth_hz;
    const auto minimum_chips=minimum_pattern_chips(base,target_snr_db_hz,tone);
    plan.target_symbol_snr_db=pattern_target_symbol_snr_db;
    const double exponent=(plan.target_symbol_snr_db-target_snr_db_hz)/10-std::log10(chip_seconds);
    plan.required_spreading=exponent>std::log10(std::numeric_limits<double>::max())?
        std::numeric_limits<double>::infinity():std::max(1.,std::pow(10.,exponent));
    if(lengths[index])base.spreading_factor=lengths[index];
    else {
        const auto found=std::lower_bound(automatic_lengths.begin(),automatic_lengths.end(),
            std::max(plan.required_spreading,static_cast<double>(minimum_chips)));
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
    plan.target_supported=base.spreading_factor>=minimum_chips &&
        plan.estimated_symbol_snr_db+1e-10>=plan.target_symbol_snr_db;
    std::ostringstream explanation;
    explanation<<"Two pattern codewords (1 raw bit/symbol), "
        <<base.spreading_factor<<" nominal chips and "<<std::setprecision(6)<<seconds
        <<" seconds/symbol; modeled Es/N0 "<<std::fixed<<std::setprecision(1)
        <<plan.estimated_symbol_snr_db<<" dB, target "<<plan.target_symbol_snr_db<<" dB. "
        <<"Automatic selection reserves at least "<<minimum_chips<<" chips at this rate and C/N0. "
        <<"Pattern floors are 16 chips at 30 dB in-band SNR, 32 at 24 dB, otherwise 64; tones retain 64. "
        <<"Short automatic profiles also require whole chips to preserve the update cadence. ";
    explanation<<"Private profiles with compact orthogonal receive bins reserve at least 32 chips. ";
    explanation<<"Other profiles beyond the 256-sample exact-fit range retain 64 chips. ";
    if(!plan.target_supported)explanation<<"The forced length is preserved but does not meet the standalone pattern confidence target. ";
    if(tone)explanation<<"Tone modes are unencrypted and do not provide Low-Probability-of-Intercept protection. ";
    if(mode==PatternMode::auto_keystream && !encryption)explanation<<"Without a key, auto-pattern is used. ";
    explanation<<"The 18 dB integration target is an initial model, not calibrated detection sensitivity or a false-alarm guarantee. Acquisition uses received pattern evidence. Both endpoints derive the profile from matching rate, carrier, C/N0 and pattern settings.";
    plan.explanation=explanation.str();
    return plan;
}
}
modem::OuterDsssVersion parse_outer_dsss_version(std::string_view name) {
    if(name=="legacy")return modem::OuterDsssVersion::legacy_v1;
    if(name=="interleave"||name=="interleaved-v2")return modem::OuterDsssVersion::interleaved_v2;
    throw Error("dsss-version must be interleave or legacy (diagnostic)");
}
std::string_view outer_dsss_version_id(modem::OuterDsssVersion version) {
    switch(version) {
    case modem::OuterDsssVersion::legacy_v1:return "legacy";
    case modem::OuterDsssVersion::interleaved_v2:return "interleave";
    }
    throw Error("invalid outer DSSS waveform version");
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
Plan resolve(double bandwidth_hz,double target_snr_db_hz,PatternMode mode,bool encryption,
             std::optional<double> carrier_hz,unsigned dsss_factor,modem::OuterDsssVersion version,
             modem::SearchArithmetic arithmetic) {
    modem::Config config;
    // Validate even enum-valued callers; matrix4 remains a disabled placeholder.
    (void)search_arithmetic_id(arithmetic); // Validate while preserving internal diagnostic enums.
    if(arithmetic==modem::SearchArithmetic::matrix4)throw Error("4-bit matrix search arithmetic is not implemented");
    config.search_arithmetic=arithmetic;
    config.dsss_factor=dsss_factor;
    (void)outer_dsss_version_id(version);
    config.outer_dsss_version=dsss_factor>1?version:modem::OuterDsssVersion::legacy_v1;
    config.bandwidth_hz=bandwidth_hz;
    config.carrier_hz=carrier_hz.value_or(recommended_carrier_hz(modem::waveform_bandwidth_hz(config)));
    // Real passband PCM must sample the actual carrier independently of the
    // hardware audio clock, including explicit high-frequency carriers.
    config.sample_rate=recommended_sample_rate(modem::waveform_bandwidth_hz(config),config.carrier_hz);
    return resolve_config(std::move(config),target_snr_db_hz,mode,encryption);
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
        // Decide the integration floor using the caller's actual carrier,
        // clock and DSSS geometry, rather than a temporary default profile.
        auto config=resolve_config(base,target,mode,encryption).config;
        const auto duplicate=std::any_of(profiles.begin(),profiles.end(),[&](const auto& prior) {
            return prior.sample_rate==config.sample_rate && prior.carrier_hz==config.carrier_hz &&
                prior.bandwidth_hz==config.bandwidth_hz && prior.constellation_bits==config.constellation_bits &&
                prior.pattern_symbols==config.pattern_symbols && prior.spreading_factor==config.spreading_factor &&
                modem::symbol_sample_count(prior)==modem::symbol_sample_count(config) && prior.spreading_mode==config.spreading_mode &&
                prior.pulse_shaping==config.pulse_shaping &&
                prior.scramble==config.scramble && prior.dsss==config.dsss && prior.dsss_factor==config.dsss_factor &&
                prior.outer_dsss_version==config.outer_dsss_version;
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
std::span<const OscillatorPreset> oscillator_presets(){return oscillator_models;}
OscillatorPreset parse_oscillator_preset(std::string_view name) {
    const auto wanted=normalized(name);
    for(const auto& preset:oscillator_models)
        if(normalized(preset.id)==wanted || normalized(preset.name)==wanted)return preset;
    throw Error("unknown oscillator preset: "+std::string(name));
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
