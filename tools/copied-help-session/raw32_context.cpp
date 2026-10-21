#include "raw32_context.hpp"
#include <algorithm>
#include "datapump/tuning.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace datapump::diagnostic::copied_help_session {

static std::string bank_key_tag(const Crypto& key) {
    const auto bytes=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
    constexpr char digits[]="0123456789abcdef";
    std::string result; result.reserve(bytes.size()*2);
    for(const auto byte:bytes) {result+=digits[byte>>4];result+=digits[byte&15];}
    return result;
}

// Fixed run1 context. The caller/root must independently verify the retained
// full keyfile SHA256 from the accepted capture before invoking this factory.
// A fresh Session executes this configuration; it does not recreate the prior
// eighteen GUI phases, wall epoch, source RNG position or staged row history.
Context make_context(const std::filesystem::path& retained_run1_keyfile) {
    constexpr std::uint64_t captured_workspace=7832123392ULL;
    constexpr auto selected_tag="31c2e901c8a03306e7b0dee1abdb16ad589d43ecd0d973f19d2d257fba89ce3f";
    if(captured_workspace>std::numeric_limits<std::size_t>::max())
        throw Error("captured copied-Help context requires a 64-bit workspace size");
    if(!retained_run1_keyfile.is_absolute())throw Error("retained copied-Help keyfile must be an absolute path");
    const auto keys=load_keyring(retained_run1_keyfile);
    if(keys.size()!=2 || keys[0].name!="A|B" || keys[1].name!="None" || bank_key_tag(keys[0].key)!=selected_tag)
        throw Error("copied-Help context requires the retained run1 A|B, None keyring and selected key identity");

    Context context;
    auto& value=context.settings;
    value.simulation=true;
    value.device="default";
    value.mono=true;value.channel_mode=audio::ChannelMode::left_mono;
    value.full_duplex=false;value.exclusive=false;value.transmit_gain=1.;
    value.simulation_seed=1;value.simulation_speed=1;
    value.content_limit=default_memory_limit;
    value.dsp_workspace_bytes=static_cast<std::size_t>(captured_workspace);
    auto& options=value.transfer;
    options.content_limit=value.content_limit;
    options.dsp_workspace_bytes=value.dsp_workspace_bytes;
    options.fec=FecMode::rs20;options.compression=true;
    options.timestamp=0;options.capture_epoch.reset();options.search_seconds=0;
    options.clock_sync.reset();options.audio_timing_error_seconds=clock_sync::default_audio_error_seconds;
    options.automatic_receive_profiles=true;
    options.receive_pattern_mode=tuning::PatternMode::auto_pattern;
    options.receive_targets_db_hz={32,55};
    options.key=keys[0].key;
    for(const auto& entry:keys)value.receive_keys.push_back(entry.key);

    // Same unseeded production tuning calls as Controller::configure. Live /
    // transfer::seeded_config binds the Data/Scrambler streams later ONCE.
    const auto arithmetic=tuning::parse_search_arithmetic("default");
    const auto dsss_version=tuning::parse_outer_dsss_version("interleave");
    auto short_plan=tuning::resolve(3600,32,tuning::PatternMode::auto_pattern,true,1500,1,dsss_version,arithmetic);
    auto long_plan=tuning::resolve(3600,55,tuning::PatternMode::auto_pattern,true,1500,1,dsss_version,arithmetic);
    modem::OscillatorSearchConfig oscillator;
    oscillator.lf=tuning::oscillator_model(tuning::parse_oscillator_preset("crystal"));
    oscillator.rf=modem::OscillatorModel{0,0};oscillator.rf_shift_hz=0;oscillator.margin=3;
    oscillator.reference=modem::OscillatorReference::independent_audio;
    oscillator.sideband=modem::OscillatorSideband::upper;
    modem::validate_oscillator_search(oscillator);
    const auto receiver_policy=[&](modem::Config& config) {
        config.receiver_strategy=tuning::parse_receiver_strategy("auto");
        config.receiver_cpu_budget=.6;
        config.scheduled_hopping.reset();config.symbol_burst={};config.frequency_search_multiplier=1;
        config.oscillator_search=oscillator;
        config.fake_hopping.reset();
        // sample_gate_bandwidth returns immediately for this absent schedule.
        if(config.scheduled_hopping || config.symbol_burst.enabled() || config.data_key)
            throw Error("copied-Help unseeded profile unexpectedly acquired calendar/key binding");
    };
    receiver_policy(short_plan.config);receiver_policy(long_plan.config);
    options.modem=short_plan.config;value.long_message_modem=long_plan.config;
    const auto effects=modem::oscillator_effects(options.modem);
    value.simulation_clock_error_ppm=effects.clock_error_ppm;
    value.simulation_frequency_offset_hz=effects.frequency_offset_hz;
    value.simulation_phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
    // Controller smoke planner: +3 dBm TX,60 dB loss,-164 dBm/Hz noise.
    const double tx_dbm=3,path_loss_db=60,noise_density_dbm_hz=-164;
    const auto cn0=tx_dbm-path_loss_db-noise_density_dbm_hz;
    value.simulation_spectrum_gain_db=tx_dbm-path_loss_db+117;
    value.simulation_snr_db=std::clamp(cn0-10*std::log10(options.modem.sample_rate/2.),-300.,300.);

    const auto profiles=tuning::receive_profiles(options.modem,options.receive_targets_db_hz,
        options.receive_pattern_mode,true);
    if(options.modem.sample_rate!=14400 || options.modem.carrier_hz!=1500 || options.modem.bandwidth_hz!=3600 ||
       options.modem.spreading_factor!=128 || !options.modem.pattern_symbols || !options.modem.scramble ||
       options.modem.dsss || options.modem.dsss_factor!=1 || !options.modem.pulse_shaping ||
       !value.long_message_modem || value.long_message_modem->spreading_factor!=64 || profiles.size()!=2 ||
       profiles[0].spreading_factor!=128 || profiles[1].spreading_factor!=64)
        throw Error("source37 smoke tuning/profile identity changed");
    live::validate_settings(value);
    std::ostringstream identity;
    identity<<std::setprecision(17)
        <<"{\"event\":\"session_context\",\"source\":\"37f4315c553b9fc3807f2f173d9a0fae3f239ebc\","
        <<"\"selected_name\":\"A|B\",\"selected_bank_key_tag\":\""<<selected_tag<<"\",\"receive_keys\":2,"
        <<"\"receive_bank_key_tags\":[\""<<selected_tag<<"\",\""<<bank_key_tag(keys[1].key)<<"\"],"
        <<"\"workspace_bytes\":"<<captured_workspace<<",\"workspace_origin\":\"captured 50percent budget; fixed bytes\","
        <<"\"sample_rate\":"<<options.modem.sample_rate<<",\"carrier_hz\":"<<options.modem.carrier_hz
        <<",\"bandwidth_hz\":"<<options.modem.bandwidth_hz<<",\"short_sf\":"<<options.modem.spreading_factor
        <<",\"long_sf\":"<<value.long_message_modem->spreading_factor<<",\"rx_targets\":[32,55],\"search_seconds\":0,"
        <<"\"timestamp\":0,\"custom_epoch_clock_required\":false,\"simulation_seed\":1,\"cn0_db_hz\":"<<cn0
        <<",\"simulation_snr_db\":"<<value.simulation_snr_db<<",\"clock_error_ppm\":"<<value.simulation_clock_error_ppm
        <<",\"frequency_offset_hz\":"<<value.simulation_frequency_offset_hz
        <<",\"phase_noise_degrees_per_sqrt_second\":"<<value.simulation_phase_noise_degrees_per_sqrt_second
        <<",\"unseeded_factory\":true,\"prior_smoke_history_replayed\":false}";
    context.identity_json=identity.str();
    return context;
}

// Root-owned existing recorder integration, deliberately not implemented here:
// void arm_probes(); // Declared by raw32_context.hpp; root sets output env.
// Call before Session({}, independent_replay_clock).start(context.settings).

} // namespace datapump::diagnostic::copied_help_session
