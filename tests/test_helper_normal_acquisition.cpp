#include "../src/gui/controller.hpp"
#include "../src/gui/fhss_profile.hpp"
#include "../src/gui/launch_command.hpp"
#include "../src/live_receiver_admission.hpp"
#include "../src/live_helper_prior.hpp"
#include "../src/live_helper_frontend.hpp"
#include "../src/live_simulation_clock.hpp"
#include "../src/audio_trace.hpp"
#include "datapump/channel.hpp"
#include "datapump/crypto.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace datapump;
namespace {
void require(bool value,const char* message) {if(!value)throw Error(message);}
bool close(double a,double b) {return std::abs(a-b)<=1e-9*std::max({1.,std::abs(a),std::abs(b)});}
std::string binary(const Bytes& bits) {std::string result;for(auto bit:bits)result+=bit?'1':'0';return result;}
Bytes known_bits(unsigned count=64) {
    constexpr std::uint64_t pattern=0xD6E4B39A1275CC07ULL;Bytes result;
    for(unsigned i=0;i<count;++i)result.push_back(static_cast<std::uint8_t>((pattern>>(63-i%64))&1));
    return result;
}
enum class Profile {hf,hf_crystal,hf_fhss,eme,practical,fast_auto,long_auto};
enum class Payload {bits,text,attachment};
const char* name(Profile p) {
    switch(p) {
    case Profile::hf:return "hf";
    case Profile::hf_crystal:return "hf-crystal";
    case Profile::hf_fhss:return "hf-fhss";
    case Profile::eme:return "eme";
    case Profile::practical:return "practical";
    case Profile::fast_auto:return "fast-auto";
    case Profile::long_auto:return "long-auto";
    }
    throw Error("unknown normal-acquisition profile");
}
const char* name(Payload p) {return p==Payload::bits?"bits":p==Payload::text?"text":"attachment";}
struct Options {
    std::vector<Profile> profiles{Profile::hf,Profile::eme};
    std::vector<bool> helpers{false,true};
    Payload payload=Payload::bits;
    bool metadata_only=false,ephemeral_key=false,keyfile_explicit=false,bit_count_explicit=false,wall_explicit=false;
    bool work_trace=false,runtime_metrics=false,gpsdo_reference=false,gpsdo_only=false;
    std::optional<std::size_t> workspace_bytes;
    std::optional<std::string> clock_sync_text,audio_error_text;
    std::optional<unsigned> source_bytes;
    std::optional<std::uint64_t> observe_samples;
    std::optional<std::filesystem::path> inspect_path;
    std::optional<std::uint64_t> inspect_epoch;
    std::filesystem::path keyfile="/tmp/demoKey",evidence;
    double helper_power_w=1500;
    // Preserve the original launch text unless the new manual override is used.
    std::string helper_power_text="1500.0000000000005";
    unsigned wall_seconds=20,bit_count=64;
};
Options parse(int argc,char** argv) {
    Options result;bool explicit_profiles=false;
    const auto add_profile=[&](Profile profile) {
        if(!explicit_profiles){result.profiles.clear();explicit_profiles=true;}
        if(std::find(result.profiles.begin(),result.profiles.end(),profile)==result.profiles.end())
            result.profiles.push_back(profile);
    };
    for(int i=1;i<argc;++i) {
        const std::string argument=argv[i];
        const auto value=[&]() {require(i+1<argc,"missing fixture option value");return std::string(argv[++i]);};
        if(argument=="--metadata-only")result.metadata_only=true;
        else if(argument=="--work-trace")result.work_trace=true;
        else if(argument=="--runtime-metrics")result.runtime_metrics=true;
        else if(argument=="--gpsdo-reference")result.gpsdo_reference=result.runtime_metrics=true;
        else if(argument=="--gpsdo-only") {
            result.gpsdo_only=result.gpsdo_reference=result.runtime_metrics=true;
            result.helpers={false};
        }
        else if(argument=="--workspace-bytes") {
            const auto text=value();std::size_t consumed=0;const auto bytes=std::stoull(text,&consumed);
            require(!text.empty()&&text.front()>='0'&&text.front()<='9'&&consumed==text.size()&&bytes>0&&
                    bytes<=std::numeric_limits<std::size_t>::max(),"fixture workspace must be positive bytes fitting size_t");
            result.workspace_bytes=static_cast<std::size_t>(bytes);
        }
        else if(argument=="--clock-sync")result.clock_sync_text=clock_sync::format(clock_sync::parse(value()));
        else if(argument=="--audio-error") {
            const auto text=value();const auto seconds=clock_sync::parse_audio_error(text);
            clock_sync::validate_audio_error(seconds);result.audio_error_text=text;
        }
        else if(argument=="--ephemeral-key")result.ephemeral_key=true;
        else if(argument=="--hf")add_profile(Profile::hf);
        else if(argument=="--hf-crystal")add_profile(Profile::hf_crystal);
        else if(argument=="--hf-fhss")add_profile(Profile::hf_fhss);
        else if(argument=="--eme")add_profile(Profile::eme);
        else if(argument=="--practical")add_profile(Profile::practical);
        else if(argument=="--fast-auto")add_profile(Profile::fast_auto);
        else if(argument=="--long-auto")add_profile(Profile::long_auto);
        else if(argument=="--keyfile") {result.keyfile=value();result.keyfile_explicit=true;}
        else if(argument=="--evidence-dir")result.evidence=value();
        else if(argument=="--inspect-trace")result.inspect_path=value();
        else if(argument=="--helper-power-w") {
            const auto text=value();std::size_t consumed=0;const auto power=std::stod(text,&consumed);
            require(consumed==text.size()&&std::isfinite(power)&&power>0,
                    "fixture enabled helper power must be finite and positive");
            result.helper_power_w=power;result.helper_power_text=text;
        }
        else if(argument=="--transmit-epoch") {
            const auto text=value();std::size_t consumed=0;const auto epoch=std::stoull(text,&consumed);
            require(!text.empty()&&text.front()>='0'&&text.front()<='9'&&consumed==text.size()&&epoch>0,
                    "posthoc transmit epoch must be a positive integer");
            result.inspect_epoch=epoch;
        }
        else if(argument=="--bit-count") {
            const auto text=value();std::size_t consumed=0;const auto count=std::stoul(text,&consumed);
            require(consumed==text.size()&&count>=1&&count<=4096,"fixture binary bit count must be 1..4096");
            result.bit_count=static_cast<unsigned>(count);result.bit_count_explicit=true;
        }
        else if(argument=="--source-bytes") {
            const auto text=value();std::size_t consumed=0;const auto count=std::stoul(text,&consumed);
            require(consumed==text.size()&&count>=1&&count<=4096,"fixture source size must be 1..4096 bytes");
            result.source_bytes=static_cast<unsigned>(count);
        }
        else if(argument=="--observe-samples") {
            const auto text=value();std::size_t consumed=0;const auto count=std::stoull(text,&consumed);
            // Manual observation only: this frontier never enters receiver
            // geometry, payload length, source recovery or completion.
            constexpr std::uint64_t maximum=3600ULL*6000;
            require(!text.empty()&&text.front()>='0'&&text.front()<='9'&&consumed==text.size()&&count>=1&&count<=maximum,
                    "manual observation sample frontier must be 1..21600000");
            result.observe_samples=count;
        }
        else if(argument=="--wall-seconds") {
            const auto text=value();std::size_t consumed=0;const auto seconds=std::stoul(text,&consumed);
            // This manual diagnostic allowance changes no CTest timeout or
            // receive completion rule. Exhaustion still reports incomplete.
            require(consumed==text.size()&&seconds>=1&&seconds<=3600,"fixture wall bound must be 1..3600 seconds per case");
            result.wall_seconds=static_cast<unsigned>(seconds);result.wall_explicit=true;
        }
        else if(argument=="--helper") {const auto selected=value();require(selected=="off"||selected=="on"||selected=="both","helper fixture choice must be off, on or both");result.helpers=selected=="off"?std::vector<bool>{false}:selected=="on"?std::vector<bool>{true}:std::vector<bool>{false,true};}
        else if(argument=="--payload") {const auto selected=value();require(selected=="bits"||selected=="text"||selected=="attachment","fixture payload must be bits, text or attachment");result.payload=selected=="bits"?Payload::bits:selected=="text"?Payload::text:Payload::attachment;}
        else throw Error("unknown normal-acquisition fixture argument: "+argument);
    }
    require(!result.ephemeral_key||!result.keyfile_explicit,"--ephemeral-key cannot replace an explicit --keyfile");
    require(!result.bit_count_explicit||result.payload==Payload::bits,"--bit-count applies only to the binary payload scenario");
    require(!result.source_bytes||result.payload!=Payload::bits,"--source-bytes applies only to text or attachment payloads");
    require(result.inspect_path.has_value()==result.inspect_epoch.has_value(),
            "posthoc inspection requires both --inspect-trace and --transmit-epoch");
    if(result.observe_samples)require(!result.metadata_only&&!result.inspect_path&&result.profiles.size()==1&&
            result.helpers.size()==1&&result.wall_explicit,
            "manual observation requires one profile/helper state and explicit --wall-seconds, without metadata or posthoc inspection");
    if(result.inspect_path) {
        require(!result.metadata_only&&!result.ephemeral_key&&result.keyfile_explicit&&result.profiles.size()==1&&result.helpers.size()==1&&
                result.payload==Payload::bits&&result.bit_count_explicit,
                "posthoc inspection requires one explicit profile/helper state, actual key and explicit binary bit count");
        require(std::filesystem::is_regular_file(*result.inspect_path)&&
                std::filesystem::file_size(*result.inspect_path)<=audio::trace::State::maximum_file_bytes,
                "posthoc inspection requires an existing bounded trace file");
    }
    require(!result.gpsdo_reference||(result.workspace_bytes&&
            result.helpers==(result.gpsdo_only?std::vector<bool>{false}:std::vector<bool>{false,true})&&
            !result.observe_samples&&!result.inspect_path),
            "GPSDO reference requires an explicit byte workspace and its declared helper-OFF/ON pair or standalone control");
    if(result.evidence.empty())result.evidence=std::filesystem::temp_directory_path()/"datapump-helper-normal-acquisition";
    return result;
}
struct EphemeralKey {
    std::filesystem::path directory,path;
    explicit EphemeralKey(Options& options) {
        if(!options.ephemeral_key)return;
        directory=std::filesystem::temp_directory_path()/("datapump-helper-normal-key-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+
            std::to_string(audio::trace::identity()));
        require(std::filesystem::create_directory(directory),"could not create a unique ephemeral key directory");
        path=directory/"named-key.bin";
        try {create_keyring(path,{"Default"});options.keyfile=path;}
        catch(...) {std::error_code ignored;std::filesystem::remove(path,ignored);std::filesystem::remove(directory,ignored);throw;}
    }
    ~EphemeralKey() {
        if(directory.empty())return;
        std::error_code ignored;std::filesystem::remove(path,ignored);std::filesystem::remove(directory,ignored);
    }
};
std::vector<std::string> launch_arguments(Profile profile,bool helper,const Options& options) {
    // HF and EME retain the user's complete commands. Practical is a separate
    // diagnostic profile. Only helper power differs within a matched pair;
    // simulation is selected by Controller::Options.
    std::vector<std::string> args{
        "--pattern","auto-keystream","--tx-dbm","36.020599913279625",
        "--helper-power",helper?options.helper_power_text:"0","--helper-ratio-limit","90",
        "--helper-frequency","high","--helper-callsign=","--noise-dbm-hz","-164",
        "--search-margin","3","--reference",profile==Profile::hf_crystal?"shared-radio":"independent","--doppler","0.000000c",
        "--clock-sync",options.clock_sync_text.value_or("default"),"--audio-error",options.audio_error_text.value_or("0ms"),"--dsss-factor","10",
        "--dsss-version","interleave","--search-arithmetic","default",
        "--receiver-strategy","auto","--receiver-cpu-budget","0.6","--live-duplex","yes",
        "--sched-hop-dilution","0","--sched-hop","0","--sched-frequency-search","1",
        "--symbol-burst","off","--symbol-burst-random","0","--keyfile="+options.keyfile.string(),
        "--key-name=Default","--tx-key","named"};
    const std::vector<std::string> specific=profile==Profile::hf?std::vector<std::string>{
        "--path-loss-db","180","--oscillator","crystal","--rf-oscillator","ic-7100",
        "--shift","3e+07","--target-snr","8","--rate","36","--carrier","3.00015e+07",
        "--dsp-workspace","50%","--dsss-mode","interleave","--fhss","off"}:
        profile==Profile::hf_crystal?std::vector<std::string>{
        "--path-loss-db","180","--oscillator","crystal","--rf-oscillator","crystal",
        "--shift","3e+07","--target-snr","8","--rate","36","--carrier","3.00015e+07",
        "--dsp-workspace","50%","--dsss-mode","interleave","--fhss","off"}:
        profile==Profile::hf_fhss?std::vector<std::string>{
        "--path-loss-db","60","--oscillator","crystal","--rf-oscillator","ic-7100",
        "--shift","3e+07","--target-snr","40","--rate","360","--carrier","3.00015e+07",
        "--dsp-workspace","50%","--dsss-mode","interleave","--fhss","fake-fcc"}:
        profile==Profile::eme?
        std::vector<std::string>{
        "--path-loss-db","205","--oscillator","gpsdo-ocxo","--rf-oscillator","gpsdo-ocxo",
        "--shift","5.8e+09","--target-snr","-33.19739098858173",
        "--short-target-snr","-3.0720996964786846","--long-target-snr","-3.0720996964786846",
        "--rate","1","--carrier","5.8000015e+09","--dsp-workspace","75%",
        "--dsss-mode","off","--fhss","fake-fcc"}:
        profile==Profile::practical?std::vector<std::string>{
        "--path-loss-db","160","--oscillator","crystal","--rf-oscillator","ic-7100",
        "--shift","1e+06","--target-snr","22","--rate","1000","--carrier","1001500",
        "--dsp-workspace","50%","--dsss-mode","off","--fhss","off"}:
        profile==Profile::fast_auto?std::vector<std::string>{
        "--path-loss-db","130","--oscillator","crystal","--rf-oscillator","ic-7100",
        "--shift","3e+07","--target-snr","55","--rate","1000","--carrier","30001500",
        "--dsp-workspace","50%","--dsss-mode","off","--fhss","off"}:
        std::vector<std::string>{
        // A distinct long-payload fixture. Target 56 reaches the ordinary
        // 24 dB in-band floor for 32 chips; the physical link level is unchanged.
        "--path-loss-db","130","--oscillator","crystal","--rf-oscillator","ic-7100",
        "--shift","3e+07","--target-snr","56","--rate","1500","--carrier","30001500",
        "--dsp-workspace","50%","--dsss-mode","off","--fhss","off"};
    args.insert(args.end(),specific.begin(),specific.end());return args;
}
struct Resolved {
    live::Settings settings;
    gui::launch_command::Patch patch;
    std::vector<std::string> arguments;
    double configured_helper_power_w=1500;
    bool gpsdo_reference=false;
};
struct PriorGeometry {
    std::size_t clocks=0,selected=0,represented_clocks=0;
    double reference_uncertainty=.5,offset=0,radius=0,clock_cell_half_width_ppm=0;
    bool admitted=false;
};
PriorGeometry predicted_prior_geometry(const live::Settings& value,const modem::OscillatorPatternSearch& search,
                                      double configured_helper_power_w=1500) {
    PriorGeometry result;const auto& c=value.transfer.modem;
    auto options=value.helper;options.power_w=configured_helper_power_w;
    const auto width=std::max(helper::occupied_bandwidth(c),helper::occupied_bandwidth(*value.long_message_modem));
    const auto plan=helper::plan(options,helper::placement_band(c,width),120000000,0,modem::outer_dsss_transmit_gain(c));
    // Geometry prediction only: a preceding measured tone with a 0.5 Hz
    // uncertainty must still pass the runtime presence/frequency gates.
    result.offset=value.simulation_frequency_offset_hz+
        plan.frequency_hz*value.simulation_clock_error_ppm*1e-6;
    const auto prior=live::detail::helper_prior_mask(search.hypotheses,result.offset,result.reference_uncertainty,
        c.carrier_hz-plan.frequency_hz,.125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c)),
        search.clock_step_ppm/2,search.requested_clock_half_width_ppm);
    result.clocks=prior.rates;result.selected=prior.selected;result.represented_clocks=prior.represented_rates;
    result.radius=prior.radius_hz;result.clock_cell_half_width_ppm=prior.clock_cell_half_width_ppm;
    result.admitted=prior.admitted;return result;
}
void auto_geometry(Profile profile,const live::Settings& value,bool helper_enabled,double configured_helper_power_w) {
    const auto& c=value.transfer.modem;
    const auto domain=modem::oscillator_pattern_search(c,
        transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(c):0);
    const auto prior=predicted_prior_geometry(value,domain,configured_helper_power_w);
    std::vector<Bytes> tags;
    const auto add_key=[&](const Crypto& key) {
        const auto tag=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
        if(std::find(tags.begin(),tags.end(),tag)==tags.end())tags.push_back(tag);
    };
    add_key(*value.transfer.key);for(const auto& key:value.receive_keys)add_key(key);
    const auto profiles=tuning::receive_profiles(c,value.transfer.receive_targets_db_hz,
        value.transfer.receive_pattern_mode,true);
    require(!profiles.empty()&&!value.permits_plaintext(),"Auto metadata lost its encrypted automatic profiles");
    const auto capacity=live::receiver_workspace_capacity(value);
    auto options=value.helper;options.power_w=configured_helper_power_w;
    const auto width=std::max(helper::occupied_bandwidth(c),helper::occupied_bandwidth(*value.long_message_modem));
    const auto tone=helper::plan(options,helper::placement_band(c,width),120000000,0,modem::outer_dsss_transmit_gain(c)).frequency_hz;
    helper::PayloadFilter filter(c.sample_rate,tone-helper::auxiliary_pilot_spacing_hz/2,425);
    modem::ChannelConfig impairments;
    impairments.snr_db=value.simulation_snr_db;impairments.clock_error_ppm=value.simulation_clock_error_ppm;
    impairments.frequency_offset_hz=value.simulation_frequency_offset_hz;
    impairments.phase_noise_degrees_per_sqrt_second=value.simulation_phase_noise_degrees_per_sqrt_second;
    impairments.seed=value.simulation_seed;
    // Query the actual bounded layout without reading or generating PCM.
    // Physical helper transport has more spare storage than Legacy; neither
    // mode changes the existing total channel/plot reservation.
    std::optional<modem::SampledSimulationChannel::PreconversionCapture> raw_capture;
    if(value.helper.power_w>0)raw_capture=live::detail::shared_helper_capture(c,tone);
    const modem::SampledSimulationChannel channel(c,impairments,value.helper,width,
        modem::SampledSimulationChannel::HelperEnvelope{},raw_capture);
    const auto reserved=channel.helper_cohort_workspace_bytes();
    require(reserved<=modem::SampledSimulationChannel::workspace_bound&&
            modem::SampledSimulationChannel::auxiliary_workspace_bound<=modem::SampledSimulationChannel::workspace_bound-reserved&&
            channel.working_bytes()<=modem::SampledSimulationChannel::workspace_bound-reserved-
                modem::SampledSimulationChannel::auxiliary_workspace_bound,
            "actual channel layout exceeds its unchanged channel/plot workspace reserve");
    // This catches a filter that cannot fit even without private Live control.
    // The private cohort's actual allocation is checked separately in the
    // physical trace; metadata alone cannot establish helper admission.
    const auto filter_bytes=filter.allocated_bytes();
    std::cout<<"auto_resource_geometry,"<<name(profile)<<','<<helper_enabled<<','<<c.sample_rate<<','
        <<filter_bytes<<','<<reserved<<','<<(filter_bytes<=reserved?reserved-filter_bytes:0)<<'\n';
    require(filter_bytes<reserved,"Auto helper filter alone exceeds the public cohort workspace reserve");
    for(std::size_t i=0;i<profiles.size();++i) {
        auto source_options=value.transfer;source_options.modem=profiles[i];
        const auto seeded=transfer::seeded_config(source_options,1800000000);
        live::detail::ReceiverAdmissionContext context;
        context.now=1800000000;context.keyed=true;context.key_count=tags.size();context.profile_count=profiles.size();
        context.capacity=capacity;context.control_margin=live::receiver_control_margin(seeded,true);
        context.prefer_streamed_templates=true;context.compact_clock_search=false;
        const auto prepared=live::detail::prepare_receiver_search(seeded,value,1800000000,context);
        require(capacity>context.control_margin,"Auto metadata has no receiver constructor headroom");
        const auto selection=modem::resolve_receiver_strategy(seeded,prepared.search,
            std::min(value.dsp_workspace_bytes/2,capacity-context.control_margin));
        const auto oscillator=modem::oscillator_pattern_search(seeded,
            transfer::effective_clock_sync(source_options)?modem::utc_transmit_rate_limit(seeded):0);
        const auto half=helper::occupied_bandwidth(seeded)/2;
        const auto uncertainty=oscillator.frequency.requested_half_width_hz+
            (std::abs(seeded.carrier_hz)+half)*oscillator.requested_clock_half_width_ppm*1e-6;
        const auto low=seeded.carrier_hz-half-uncertainty,high=seeded.carrier_hz+half+uncertainty;
        const auto error=filter.maximum_amplitude_error(low,high);
        std::cout<<std::setprecision(17)<<"auto_geometry_gate,"<<name(profile)<<','<<helper_enabled<<','<<i<<','
            <<selection.supported<<','<<modem::receiver_backend_name(selection.backend)<<','
            <<selection.full_frequency_coverage<<','<<selection.full_clock_coverage<<','<<oscillator.limited<<','
            <<low<<','<<high<<','<<tone<<','<<error<<','<<std::quoted(selection.reason)<<'\n';
        require(selection.supported&&selection.backend==modem::ReceiverBackend::existing_complex&&
                selection.full_frequency_coverage&&selection.full_clock_coverage&&!oscillator.limited&&
                low>0&&high<seeded.sample_rate/2.&&(high<tone-725||low>tone+325)&&error<=.001,
                "Auto metadata lacks its original backend, clock domain or full-support helper FIR certificate");
        std::cout<<std::setprecision(17)<<"auto_geometry,"<<name(profile)<<','<<helper_enabled<<','<<i<<','<<profiles.size()<<','
            <<tags.size()<<','<<modem::receiver_backend_name(selection.backend)<<','<<prepared.search.hypotheses.size()<<','
            <<selection.full_frequency_coverage<<','<<selection.full_clock_coverage<<','<<low<<','<<high<<','<<tone<<','
            <<error<<','<<filter.delay()<<'\n';
    }
    require(!domain.limited&&prior.clocks==3&&close(domain.requested_clock_half_width_ppm,300)&&
            close(domain.clock_half_width_ppm,300)&&prior.admitted&&prior.selected>0&&
            prior.selected<domain.hypotheses.size()&&prior.represented_clocks==prior.clocks,
            "Auto catalogue cannot retain the complete declared clock domain and a strict helper subset");
}
Resolved resolve(Profile profile,bool helper,const Options& options) {
    auto arguments=launch_arguments(profile,helper,options);
    const auto patch=gui::launch_command::parse_arguments(arguments);
    gui::Controller controller({true,false,patch});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!controller.settings().transfer.key&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto settings=controller.settings();controller.close();
    require(settings.transfer.key.has_value(),"actual Controller did not load the named Default transmit key");
    require(settings.simulation&&settings.full_duplex&&settings.transfer.timestamp==0,
            "normal acquisition fixture lost default rolling simulation or duplex configuration");
    require(settings.transfer.modem.receiver_strategy==modem::ReceiverStrategy::automatic,
            "Controller changed the requested Auto receiver strategy");
    require(close(settings.transfer.modem.receiver_cpu_budget,.6)&&settings.transfer.automatic_receive_profiles,
            "Controller changed the receiver CPU policy or automatic receive-profile behavior");
    require(close(settings.helper.power_w,helper?options.helper_power_w:0.)&&close(settings.helper.ratio_limit_db,90)&&
            settings.helper.frequency==helper::Frequency::high&&settings.helper.callsign.empty(),
            "Controller changed the helper power, ratio, placement or empty callsign");
    const auto& c=settings.transfer.modem;
    const auto reference=profile==Profile::hf_crystal?modem::OscillatorReference::shared_radio:
        modem::OscillatorReference::independent_audio;
    require(c.oscillator_search&&c.oscillator_search->reference==reference&&
            close(c.oscillator_search->margin,3)&&close(c.carrier_hz,1500),
            "Controller changed the declared clock topology, search margin or audio carrier");
    require(settings.long_message_modem.has_value(),"Controller omitted the configured long-message profile");
    require(settings.transfer.clock_sync==clock_sync::parse(options.clock_sync_text.value_or("default"))&&
            close(settings.transfer.audio_timing_error_seconds,clock_sync::parse_audio_error(options.audio_error_text.value_or("0ms"))),
            "Controller changed requested clock synchronization or causal audio lag");
    if(profile==Profile::hf) {
        require(patch.workspace_percent==50&&close(*patch.target_db_hz,8)&&c.sample_rate==6120&&
                close(c.bandwidth_hz,36)&&c.spreading_factor==256&&c.dsss_factor==10&&
                c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2&&
                modem::symbol_sample_count(c)==87040&&!c.fake_hopping&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,1)&&
                close(c.oscillator_search->rf_shift_hz,3e7),"HF command did not retain the user's exact physical profile");
    } else if(profile==Profile::hf_crystal) {
        require(patch.workspace_percent==50&&patch.path_loss_db&&close(*patch.path_loss_db,180)&&
                patch.target_db_hz&&close(*patch.target_db_hz,8)&&patch.rate_hz&&close(*patch.rate_hz,36)&&
                c.sample_rate==6120&&close(c.bandwidth_hz,36)&&c.spreading_factor==256&&c.dsss_factor==10&&
                c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2&&
                modem::symbol_sample_count(c)==87040&&!c.fake_hopping&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,100)&&
                close(c.oscillator_search->rf_shift_hz,3e7)&&close(settings.simulation_frequency_offset_hz,3000)&&
                close(settings.simulation_clock_error_ppm,100),
                "HF crystal command changed shared-radio coupling or its modeled physical profile");
    } else if(profile==Profile::hf_fhss) {
        // The shared tuning factory defines Rate's sampled representation;
        // this assertion checks that launch parsing kept the requested target,
        // encryption and outer code rather than guessing a new sample clock.
        auto expected=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10,
            modem::OuterDsssVersion::interleaved_v2).config;
        expected.oscillator_search=c.oscillator_search;
        const auto hops=gui::fhss::resolve(gui::fhss::Preference::fcc,expected);
        require(hops.available,"HF FHSS fixture cannot resolve its declared hopping profile");
        modem::FhssPlan hop_plan;hop_plan.channels=hops.channels;hop_plan.dwell_seconds=hops.dwell_seconds;
        for(unsigned i=0;i<hop_plan.channels;++i)hop_plan.shifts[i]=hops.center(i)-expected.carrier_hz;
        auto hop_options=settings.transfer;hop_options.modem=expected;
        expected.fake_hopping=transfer::transmit_hopping(hop_plan,hop_options);
        // A short ordinary symbol can be deferred intact through the hop
        // guards. Retain that production calendar; helper tests may not turn
        // it into an unkeyed/nondeferred source for their own convenience.
        std::cout<<std::setprecision(17)<<"acceptance_resolved_geometry,"<<name(profile)<<','<<helper<<','
            <<c.sample_rate<<','<<expected.sample_rate<<','<<c.bandwidth_hz<<','<<c.spreading_factor<<','
            <<expected.spreading_factor<<','<<modem::symbol_sample_count(c)<<','
            <<modem::symbol_sample_count(expected)<<','<<c.dsss_factor<<','<<bool(c.fake_hopping)<<','
            <<modem::deferred_symbols(c)<<','<<modem::deferred_symbols(expected)<<','
            <<c.oscillator_search->lf.accuracy_ppm<<','<<c.oscillator_search->rf.accuracy_ppm<<','
            <<c.oscillator_search->rf_shift_hz<<','<<settings.simulation_frequency_offset_hz<<','
            <<settings.simulation_clock_error_ppm<<'\n';
        require(patch.workspace_percent==50&&patch.path_loss_db&&close(*patch.path_loss_db,60)&&
                patch.target_db_hz&&close(*patch.target_db_hz,40)&&patch.rate_hz&&close(*patch.rate_hz,360)&&
                c.sample_rate==expected.sample_rate&&close(c.bandwidth_hz,360)&&
                c.spreading_factor==expected.spreading_factor&&
                modem::symbol_sample_count(c)==modem::symbol_sample_count(expected)&&c.dsss_factor==10&&
                c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2&&c.fake_hopping&&
                modem::deferred_symbols(c)==modem::deferred_symbols(expected)&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,1)&&
                close(c.oscillator_search->rf_shift_hz,3e7)&&close(settings.simulation_frequency_offset_hz,30)&&
                close(settings.simulation_clock_error_ppm,100),
                "HF FHSS command changed its independent clocks, target, Rate or sampled profile");
    } else if(profile==Profile::eme) {
        require(patch.workspace_percent==75&&patch.short_target_db_hz&&patch.long_target_db_hz&&
                close(*patch.target_db_hz,-33.19739098858173)&&
                close(*patch.short_target_db_hz,-3.0720996964786846)&&
                close(*patch.long_target_db_hz,-3.0720996964786846)&&c.sample_rate==6000&&
                close(c.bandwidth_hz,1)&&c.spreading_factor==64&&c.dsss_factor==1&&
                modem::symbol_sample_count(c)==768000&&c.fake_hopping&&!modem::deferred_symbols(c)&&
                close(c.oscillator_search->lf.accuracy_ppm,.0001)&&close(c.oscillator_search->rf.accuracy_ppm,.0001)&&
                close(c.oscillator_search->rf_shift_hz,5.8e9),"EME command lost a separate target or the user's exact physical profile");
    } else if(profile==Profile::practical) {
        require(patch.workspace_percent==50&&close(*patch.target_db_hz,22)&&c.sample_rate==6000&&
                close(c.bandwidth_hz,1000)&&c.spreading_factor==256&&c.dsss_factor==1&&
                modem::symbol_sample_count(c)==3072&&!c.fake_hopping&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,1)&&
                close(c.oscillator_search->rf_shift_hz,1e6)&&close(settings.simulation_frequency_offset_hz,1)&&
                close(settings.simulation_clock_error_ppm,100),
                "practical Controller command changed its declared physical profile");
    } else if(profile==Profile::fast_auto) {
        require(patch.workspace_percent==50&&close(*patch.target_db_hz,55)&&c.sample_rate==6000&&
                close(c.bandwidth_hz,1000)&&c.spreading_factor==32&&c.dsss_factor==1&&
                modem::symbol_sample_count(c)==384&&!c.fake_hopping&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,1)&&
                close(c.oscillator_search->rf_shift_hz,3e7)&&close(settings.simulation_frequency_offset_hz,30)&&
                close(settings.simulation_clock_error_ppm,100),
                "fast Auto Controller command changed its declared physical profile");
    } else {
        require(patch.workspace_percent==50&&close(*patch.target_db_hz,56)&&c.sample_rate==6000&&
                close(c.bandwidth_hz,1500)&&c.spreading_factor==32&&c.dsss_factor==1&&
                modem::symbol_sample_count(c)==256&&!c.fake_hopping&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,1)&&
                close(c.oscillator_search->rf_shift_hz,3e7)&&close(settings.simulation_frequency_offset_hz,30)&&
                close(settings.simulation_clock_error_ppm,100),
                "long Auto Controller command changed its declared physical profile");
    }
    require(modem::symbol_sample_count(*settings.long_message_modem)==modem::symbol_sample_count(c),
            "the separately configured short and long targets unexpectedly selected different physical geometries");
    if(profile==Profile::practical) {
        const auto search=modem::oscillator_pattern_search(c,
            transfer::effective_clock_sync(settings.transfer)?modem::utc_transmit_rate_limit(c):0);
        const auto prior=predicted_prior_geometry(settings,search,options.helper_power_w);
        require(!search.limited&&search.hypotheses.size()==49&&prior.clocks==3&&
                close(search.requested_clock_half_width_ppm,300)&&close(search.clock_half_width_ppm,300)&&
                prior.admitted&&prior.selected>0&&prior.selected<search.hypotheses.size()&&prior.represented_clocks==prior.clocks,
                "practical original catalogue lacks complete coverage or cannot narrow while retaining every clock lane");
    }
    // Fix only the random channel draw. Every physical impairment and level
    // remains the actual Controller result of the supplied command.
    settings.simulation_seed=5181;
    return {std::move(settings),patch,std::move(arguments),options.helper_power_w};
}
Resolved gpsdo_reference(Profile profile,Resolved value) {
    require(value.settings.helper.power_w==0,"GPSDO simulation reference must keep helper OFF");
    // Clone the actual declared profile. Retuning through Controller with a
    // narrower RF prior could also change the FHSS gate/sample geometry and
    // would cease to be a controlled comparison. Only these priors and their
    // corresponding simulated oscillator truth change, including long RX.
    const auto replace=[&](modem::Config& config) {
        require(config.oscillator_search.has_value(),"GPSDO reference has no declared oscillator topology");
        auto& policy=*config.oscillator_search;
        const bool ocxo=profile==Profile::eme;
        const bool shared=policy.reference==modem::OscillatorReference::shared_radio;
        policy.lf=tuning::oscillator_model(tuning::parse_oscillator_preset(ocxo?"gpsdo-ocxo":"gpsdo-xo"));
        policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset(ocxo?"gpsdo-ocxo":shared?"gpsdo-xo":"gpsdo-tcxo"));
        modem::validate(config);
    };
    const auto phase=value.settings.simulation_phase_noise_degrees_per_sqrt_second;
    replace(value.settings.transfer.modem);replace(*value.settings.long_message_modem);
    const auto effects=modem::oscillator_effects(value.settings.transfer.modem);
    require(close(phase,effects.phase_noise_degrees_per_sqrt_second),
            "GPSDO reference unexpectedly changed the declared phase-noise stress");
    value.settings.simulation_frequency_offset_hz=effects.frequency_offset_hz;
    value.settings.simulation_clock_error_ppm=effects.clock_error_ppm;
    value.settings.simulation_phase_noise_degrees_per_sqrt_second=effects.phase_noise_degrees_per_sqrt_second;
    value.gpsdo_reference=true;return value;
}
void metadata(Profile profile,bool helper,const Resolved& resolved) {
    const auto& value=resolved.settings;const auto& c=value.transfer.modem;
    if(!resolved.gpsdo_reference&&(profile==Profile::fast_auto||profile==Profile::long_auto))
        auto_geometry(profile,value,helper,resolved.configured_helper_power_w);
    if(!resolved.gpsdo_reference&&(profile==Profile::hf_crystal||profile==Profile::hf_fhss||profile==Profile::eme))
        std::cout<<"acceptance_configuration,"<<name(profile)<<','<<helper<<','<<std::setprecision(17)
            <<resolved.configured_helper_power_w<<','<<value.helper.power_w<<','
            <<std::quoted(gui::launch_command::format(resolved.patch))<<'\n';
    const auto search=modem::oscillator_pattern_search(c,
        transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(c):0);
    std::set<double> rates,frequencies;for(const auto& pair:search.hypotheses) {rates.insert(pair.clock_error_ppm);frequencies.insert(pair.frequency_offset_hz);}
    if(profile==Profile::hf_crystal||profile==Profile::hf_fhss||profile==Profile::eme) {
        // The nominal date here seeds metadata only. This constructs no
        // receiver and never supplies a receive timestamp to the live run.
        auto source=value.transfer;source.modem=c;
        const auto seeded=transfer::seeded_config(source,1800000000);
        live::detail::ReceiverAdmissionContext context;
        context.now=1800000000;context.keyed=true;
        std::set<Bytes> key_tags;
        const auto add_key=[&](const Crypto& key){key_tags.insert(key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'}));};
        add_key(*value.transfer.key);for(const auto& key:value.receive_keys)add_key(key);
        context.key_count=key_tags.size();
        context.profile_count=tuning::receive_profiles(c,source.receive_targets_db_hz,source.receive_pattern_mode,true).size();
        context.capacity=live::receiver_workspace_capacity(value);
        context.control_margin=live::receiver_control_margin(seeded,true);
        context.prefer_streamed_templates=true;
        context.compact_clock_search=modem::symbol_sample_count(c)>=60ULL*c.sample_rate;
        const auto prepared=live::detail::prepare_receiver_search(seeded,value,1800000000,context);
        const auto grant=context.capacity>context.control_margin?
            std::min(value.dsp_workspace_bytes/2,context.capacity-context.control_margin):0;
        const auto selection=modem::resolve_receiver_strategy(seeded,prepared.search,grant);
        std::cout<<std::setprecision(17)<<"acceptance_eligibility,"<<name(profile)<<','<<helper<<','
            <<context.compact_clock_search<<','<<grant<<','<<selection.supported<<','
            <<modem::receiver_backend_name(selection.backend)<<','<<selection.full_frequency_coverage<<','
            <<selection.full_clock_coverage<<','<<std::quoted(selection.reason)<<'\n';
    }
    std::cout<<std::setprecision(17)<<"metadata,"<<name(profile)<<','<<helper<<','<<c.sample_rate<<','<<c.bandwidth_hz<<','
             <<c.spreading_factor<<','<<c.dsss_factor<<','<<modem::symbol_sample_count(c)<<','
             <<static_cast<double>(modem::symbol_sample_count(c))/c.sample_rate<<','<<search.hypotheses.size()<<','
             <<frequencies.size()<<','<<rates.size()<<','<<search.frequency.requested_half_width_hz<<','
             <<search.frequency.half_width_hz<<','<<search.limited<<','<<value.dsp_workspace_bytes<<','
             <<live::receiver_workspace_capacity(value)<<','<<value.simulation_snr_db<<','
             <<value.simulation_frequency_offset_hz<<','<<value.simulation_clock_error_ppm<<','
             <<value.simulation_phase_noise_degrees_per_sqrt_second<<','<<value.receive_keys.size()<<'\n';
    if(profile==Profile::practical||profile==Profile::fast_auto||profile==Profile::long_auto) {
        const auto prior=predicted_prior_geometry(value,search,resolved.configured_helper_power_w);
        std::cout<<"prior_geometry,"<<name(profile)<<','<<helper<<','<<search.hypotheses.size()<<','
            <<prior.selected<<','<<prior.clocks<<','<<prior.represented_clocks<<','<<prior.reference_uncertainty<<','
            <<prior.offset<<','<<prior.radius<<','<<prior.clock_cell_half_width_ppm<<','<<prior.admitted<<'\n';
    }
}
// Private diagnostic storage only; it is not borrowed from the modem's DSP
// budget. Each source byte below is one observed bit (including unknown=2).
// All requested live data capacity, including old+new arrays during growth,
// fits data_bound. Metadata has one fixed, separately reported allocation.
class WireCollector {
public:
    static constexpr std::size_t data_bound=32*1024*1024,candidate_bound=4096;
    struct Candidate {
        std::uint64_t capture=0,candidate=0,instance=0,epoch=0,family=0,origin=0;
        std::uint64_t first_sample=0,first_symbol=0,phase=0,physical_first=0;
        std::unique_ptr<std::uint8_t[]> bits;
        std::size_t size=0,capacity=0;
        bool contiguous=true,consistent=true,complete=false,observed_end=false,helper_acquired=false;
        bool helper_conditioned_acquisition=false;
    };
    enum class Failure {none,allocation,data_capacity,candidate_capacity,coordinate,invalid_bits,counter};
private:
    std::mutex mutex_;
    std::unique_ptr<Candidate[]> candidates_;
    std::atomic<Failure> failure_{Failure::none};
    std::size_t count_=0,logical_=0,capacity_=0,peak_capacity_=0;
    std::uint64_t observations_=0;
    std::uint32_t rate_;
    void fail(Failure reason)noexcept {
        auto empty=Failure::none;failure_.compare_exchange_strong(empty,reason,std::memory_order_relaxed);
    }
    bool grow(Candidate& candidate,std::size_t required) {
        if(required<=candidate.capacity)return true;
        const auto doubled=candidate.capacity>data_bound/2?data_bound:2*candidate.capacity;
        const auto next=std::max(required,std::max<std::size_t>(256,doubled));
        // Both arrays remain alive during allocation/copy. Charge their full
        // element capacities rather than only the post-swap difference.
        if(next>data_bound-capacity_) {fail(Failure::data_capacity);return false;}
        auto replacement=std::make_unique<std::uint8_t[]>(next);
        peak_capacity_=std::max(peak_capacity_,capacity_+next);
        if(candidate.size)std::copy_n(candidate.bits.get(),candidate.size,replacement.get());
        candidate.bits.swap(replacement);
        replacement.reset();
        capacity_=capacity_-candidate.capacity+next;candidate.capacity=next;
        return true;
    }
    void append(const live::detail::ReceiverWireObservation& observation) {
        if(observations_==UINT64_MAX) {fail(Failure::counter);return;}
        ++observations_;
        if(observation.first_stream_symbol<observation.stream_first_symbol||
           observation.stream_first_sample>UINT64_MAX-observation.sample_origin||
           observation.physical_first!=observation.sample_origin+observation.stream_first_sample) {
            fail(Failure::coordinate);return;
        }
        if((observation.missing_slots&&!observation.bits.empty())||
           std::any_of(observation.bits.begin(),observation.bits.end(),[](auto bit){return bit>2;})) {
            fail(Failure::invalid_bits);return;
        }
        const auto relative=observation.first_stream_symbol-observation.stream_first_symbol;
        const auto piece=observation.missing_slots?observation.missing_slots:observation.bits.size();
        if(relative>data_bound||piece>data_bound-relative) {fail(Failure::data_capacity);return;}
        if(!candidates_)candidates_=std::make_unique<Candidate[]>(candidate_bound);
        auto found=std::find_if(candidates_.get(),candidates_.get()+count_,[&](const auto& candidate) {
            return candidate.capture==observation.input_revision&&candidate.candidate==observation.candidate_id;
        });
        if(found==candidates_.get()+count_) {
            if(count_==candidate_bound) {fail(Failure::candidate_capacity);return;}
            found=&candidates_[count_++];
            found->capture=observation.input_revision;found->candidate=observation.candidate_id;
            found->instance=observation.instance_id;found->epoch=observation.epoch;found->family=observation.family;
            found->origin=observation.sample_origin;found->first_sample=observation.stream_first_sample;
            found->first_symbol=observation.stream_first_symbol;found->phase=observation.stream_phase_samples;
            found->physical_first=observation.physical_first;
        }
        auto& wire=*found;
        if(wire.instance!=observation.instance_id||wire.epoch!=observation.epoch||wire.family!=observation.family||
           wire.origin!=observation.sample_origin||wire.first_sample!=observation.stream_first_sample||
           wire.first_symbol!=observation.stream_first_symbol||wire.physical_first!=observation.physical_first) {
            fail(Failure::coordinate);return;
        }
        const auto end=static_cast<std::size_t>(relative+piece);
        const auto next_size=std::max(wire.size,end);
        if(next_size-wire.size>data_bound-logical_) {fail(Failure::data_capacity);return;}
        if(!grow(wire,next_size))return;
        if(wire.complete&&end>wire.size)wire.consistent=false;
        if(relative>wire.size) {
            std::fill(wire.bits.get()+wire.size,wire.bits.get()+static_cast<std::size_t>(relative),2);
            wire.contiguous=false;
        }
        for(std::size_t i=0;i<static_cast<std::size_t>(piece);++i) {
            const auto offset=static_cast<std::size_t>(relative)+i;
            const auto bit=observation.missing_slots?std::uint8_t{2}:observation.bits[i];
            if(offset<wire.size)wire.consistent=wire.consistent&&wire.bits[offset]==bit;
            else wire.bits[offset]=bit;
        }
        logical_+=next_size-wire.size;wire.size=next_size;
        wire.helper_acquired=wire.helper_acquired||observation.helper_acquired;
        wire.helper_conditioned_acquisition=wire.helper_conditioned_acquisition||observation.helper_conditioned_acquisition;
        if(observation.complete) {
            wire.complete=true;
            wire.observed_end=observation.feed_end_sample>=observation.physical_end&&
                observation.feed_end_sample-observation.physical_end>=6ULL*rate_;
        }
    }
public:
    explicit WireCollector(std::uint32_t rate):rate_(rate) {}
    void observe(const live::detail::ReceiverWireObservation& observation)noexcept {
        try {
            std::lock_guard lock(mutex_);
            if(failure_.load(std::memory_order_relaxed)==Failure::none)append(observation);
        }catch(const std::bad_alloc&) {fail(Failure::allocation);}
        catch(...) {fail(Failure::coordinate);}
    }
    // Read only after Session::stop/destruction and Dates uninstallation have
    // joined every callback. No candidate data is copied for final comparison.
    std::span<const Candidate> candidates()const {return {candidates_.get(),count_};}
    bool complete()const {return failure_.load(std::memory_order_relaxed)==Failure::none&&observations_>0;}
    const Candidate* find(std::uint64_t capture,std::uint64_t candidate)const {
        for(const auto& wire:candidates())if(wire.capture==capture&&wire.candidate==candidate)return &wire;
        return nullptr;
    }
    void report(const std::filesystem::path& trace)const {
        static constexpr std::array labels{"none","allocation","data_capacity","candidate_capacity","coordinate","invalid_bits","counter"};
        std::cout<<"wire_collector,"<<trace.string()<<','<<complete()<<','<<labels[static_cast<std::size_t>(failure_.load())]<<','
            <<observations_<<','<<count_<<','<<logical_<<','<<capacity_<<','<<peak_capacity_<<','<<data_bound<<','
            <<(candidates_?candidate_bound*sizeof(Candidate):0)<<','<<sizeof(WireCollector)<<','<<candidate_bound<<'\n';
    }
};
struct ProcessStamp {
    long double wall=0;
    double cpu=std::numeric_limits<double>::quiet_NaN();
    static ProcessStamp now() noexcept {
        const auto cpu=std::clock();
        return {std::chrono::duration<long double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
            cpu==std::clock_t(-1)?std::numeric_limits<double>::quiet_NaN():static_cast<double>(cpu)/CLOCKS_PER_SEC};
    }
};
struct RuntimeMetrics {
    static constexpr std::size_t capacity=65536;
    struct Feed {
        std::uint64_t source_begin=0,source_end=0,bank_begin=0,bank_end=0,revision=0;
        std::uint64_t last_commit_bank=0,partial_commits=0,empty_commits=0;
        ProcessStamp read,feed,commit;
        bool fed=false;
    };
    struct Poll {
        std::uint64_t capture=0,id=0,revision=0,bits=0;
        ProcessStamp time;
        bool replay=false;
    };
    // Test-only bounded heap; never charged to or borrowed from DSP workspace.
    std::unique_ptr<Feed[]> feeds=std::make_unique<Feed[]>(capacity);
    std::unique_ptr<Poll[]> polls=std::make_unique<Poll[]>(capacity);
    std::size_t feed_count=0,poll_count=0;
    std::uint64_t invalid=0,feed_overflow=0,poll_overflow=0,unfinished_reads=0;
    std::uint64_t partial_commits=0,empty_commits=0,standalone_commits=0;
    std::uint64_t standalone_empty_commits=0,standalone_advancing_commits=0;
    std::optional<std::pair<std::uint64_t,std::uint64_t>> last_commit;
    std::optional<Feed> pending;
    std::optional<ProcessStamp> first_read;
    ProcessStamp begin=ProcessStamp::now(),before_stop,joined,posthoc;
    std::uint32_t rate;
    explicit RuntimeMetrics(std::uint32_t rate):rate(rate) {}
    void observe(const live::detail::SimulationObservation& observation) noexcept {
        using Phase=live::detail::SimulationPhase;
        if(observation.phase==Phase::before_read) {
            if(pending)++unfinished_reads;
            pending=Feed{};pending->source_begin=observation.source;pending->bank_begin=observation.bank;
            pending->last_commit_bank=observation.bank;
            pending->revision=observation.revision;pending->read=ProcessStamp::now();
            if(!first_read)first_read=pending->read;
        } else if(observation.phase==Phase::before_feed) {
            if(!pending||pending->fed||pending->revision!=observation.revision||
               pending->bank_begin!=observation.bank||observation.source<pending->source_begin) {++invalid;return;}
            pending->source_end=observation.source;pending->feed=ProcessStamp::now();pending->fed=true;
        } else if(observation.phase==Phase::after_commit) {
            // feed_samples may split one source read at an acquisition fence.
            // An empty acquisition batch also commits an unchanged frontier.
            // Neither callback closes the packet or represents extra audio.
            const auto preceding=last_commit;
            last_commit=std::pair{observation.revision,observation.bank};
            if(!pending||!pending->fed) {
                ++standalone_commits;
                if(preceding&&preceding->first==observation.revision&&preceding->second==observation.bank)
                    ++standalone_empty_commits;
                else {++standalone_advancing_commits;++invalid;}
                // No before_feed means its numeric work has no measured start
                // checkpoint. Report it explicitly; do not invent a duration.
                return;
            }
            if(pending->revision!=observation.revision||observation.bank<pending->last_commit_bank||
               observation.bank<pending->bank_begin||
               observation.bank-pending->bank_begin>pending->source_end-pending->source_begin) {++invalid;return;}
            const bool empty=observation.bank==pending->last_commit_bank;
            pending->last_commit_bank=observation.bank;
            if(empty) {++pending->empty_commits;++empty_commits;}
            if(observation.bank-pending->bank_begin<pending->source_end-pending->source_begin) {
                if(!empty) {++pending->partial_commits;++partial_commits;}
                return;
            }
            pending->bank_end=observation.bank;pending->commit=ProcessStamp::now();
            if(feed_count<capacity)feeds[feed_count++]=*pending;else ++feed_overflow;
            pending.reset();
        }
    }
    void poll(const live::Snapshot& snapshot) noexcept {
        const auto now=ProcessStamp::now();
        for(const auto& signal:snapshot.signals) {
            if(signal.helper_carrier||!signal.received_bits)continue;
            if(poll_count==capacity){++poll_overflow;continue;}
            polls[poll_count++]={snapshot.receiver_input_revision,signal.id,signal.revision,signal.received_bits,now,snapshot.simulation_replay};
        }
    }
    static void interval(ProcessStamp first,ProcessStamp last) {
        const bool valid=std::isfinite(first.cpu)&&std::isfinite(last.cpu)&&last.cpu>=first.cpu&&last.wall>=first.wall;
        if(valid)std::cout<<','<<last.cpu-first.cpu<<','<<last.wall-first.wall;
        else std::cout<<",unavailable,unavailable";
    }
    void report(Profile profile,bool helper,bool reference,const std::filesystem::path& trace)const {
        const auto control=reference?"gpsdo-simulation-reference":"declared";
        std::cout<<"runtime_cpu,"<<name(profile)<<','<<helper<<','<<control<<','<<feed_count<<','<<invalid<<','<<feed_overflow<<','
            <<poll_overflow<<','<<unfinished_reads<<','<<(pending?1:0);
        if(first_read) {interval(begin,*first_read);interval(*first_read,before_stop);}
        else std::cout<<",unavailable,unavailable,unavailable,unavailable";
        interval(before_stop,joined);interval(joined,posthoc);
        std::cout<<",unavailable,unavailable,"<<trace.string()<<'\n';
        // Process CPU covers every thread. These are observed timestamp
        // intervals, never numeric work counts converted into CPU estimates.
        // Clip source intervals at before_stop: a concurrent final callback
        // may finish during join, which must not duplicate active CPU.
        const auto active=first_read?before_stop.cpu-first_read->cpu:0.;
        const auto clipped=[&](ProcessStamp first,ProcessStamp last) {
            if(!first_read||!std::isfinite(first.cpu)||!std::isfinite(last.cpu)||
               last.cpu<first.cpu||last.wall<first.wall)return std::numeric_limits<double>::quiet_NaN();
            return std::max(0.,std::min(last.cpu,before_stop.cpu)-std::max(first.cpu,first_read->cpu));
        };
        double generation=0,feeding=0;bool intervals_valid=first_read.has_value();
        ProcessStamp previous=first_read.value_or(begin);
        for(std::size_t i=0;i<feed_count;++i) {
            const auto& f=feeds[i];
            intervals_valid=intervals_valid&&f.read.cpu>=previous.cpu&&f.read.wall>=previous.wall&&
                f.feed.cpu>=f.read.cpu&&f.feed.wall>=f.read.wall&&
                f.commit.cpu>=f.feed.cpu&&f.commit.wall>=f.feed.wall;
            generation+=clipped(f.read,f.feed);feeding+=clipped(f.feed,f.commit);previous=f.commit;
        }
        if(pending) {
            intervals_valid=intervals_valid&&pending->read.cpu>=previous.cpu&&pending->read.wall>=previous.wall;
            if(pending->fed) {
                generation+=clipped(pending->read,pending->feed);
                feeding+=clipped(pending->feed,before_stop);
            } else generation+=clipped(pending->read,before_stop);
        }
        const auto outside=active-generation-feeding;
        intervals_valid=intervals_valid&&std::isfinite(active)&&active>=0&&std::isfinite(outside)&&outside>=-1e-8;
        const auto total=posthoc.cpu-begin.cpu;
        const auto phase_sum=first_read?first_read->cpu-begin.cpu+active+
            joined.cpu-before_stop.cpu+posthoc.cpu-joined.cpu:std::numeric_limits<double>::quiet_NaN();
        std::cout<<"runtime_commit_accounting,"<<name(profile)<<','<<helper<<','<<control<<','
            <<partial_commits<<','<<empty_commits<<','<<standalone_commits<<','<<standalone_empty_commits<<','
            <<standalone_advancing_commits<<','<<intervals_valid<<','<<generation<<','<<feeding<<','<<outside<<','
            <<total<<','<<phase_sum<<','<<total-phase_sum<<",unavailable,"<<trace.string()<<'\n';
        for(std::size_t i=0;i<feed_count;++i) {
            const auto& f=feeds[i];
            std::cout<<"runtime_feed,"<<name(profile)<<','<<helper<<','<<control<<','<<i<<','<<f.revision<<','
                <<f.source_begin<<','<<f.source_end<<','<<f.bank_begin<<','<<f.bank_end<<','<<rate<<','
                <<f.read.wall<<','<<f.feed.wall<<','<<f.commit.wall;
            interval(f.read,f.feed);interval(f.feed,f.commit);
            std::cout<<','<<trace.string()<<'\n';
        }
    }
};
struct Dates {
    std::atomic<std::uint64_t> transmit{0};
    WireCollector* collector=nullptr;
    RuntimeMetrics* metrics=nullptr;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& observation) {
        auto& self=*static_cast<Dates*>(opaque);
        if(observation.phase==live::detail::SimulationPhase::transmit_epoch)
            self.transmit=static_cast<std::uint64_t>(observation.epoch);
        if(self.metrics)self.metrics->observe(observation);
    },1800000000.125};
    explicit Dates(WireCollector* wire=nullptr,RuntimeMetrics* metrics=nullptr):collector(wire),metrics(metrics) {
        if(collector)hook.wire_callback=[](void* opaque,const live::detail::ReceiverWireObservation& observation)noexcept {
            static_cast<Dates*>(opaque)->collector->observe(observation);
        };
        live::detail::SimulationHook* empty=nullptr;
        require(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"simulation date hook already installed");
    }
    ~Dates(){live::detail::simulation_hook.store(nullptr);}
};
std::atomic<bool> trace_detail{true};
std::atomic<bool> trace_work{false};
std::atomic<bool> trace_tx_diagnostics{false};
bool trace_filter(const char* event) noexcept {
    const std::string_view name{event};
    if(trace_tx_diagnostics.load(std::memory_order_relaxed)&&name=="receiver_pattern_drain")return true;
    if(trace_work.load(std::memory_order_relaxed)&&(name=="receiver_helper_raw_reference"||name=="receiver_helper_pilot_phase"||
            name=="receiver_acquisition_work"||
            name=="receiver_helper_compact_work"||name=="receiver_helper_cohort_dispatch"||
            name=="receiver_helper_cohort_prior_decision"||name=="receiver_helper_input_view_admission"))return true;
    if(!trace_detail.load(std::memory_order_relaxed))return name=="trace_begin"||name=="receiver_signal_commit"||
        name=="receiver_bank_admitted"||name=="receiver_selection"||
        name=="receiver_helper_cohort_frontend"||name=="receiver_helper_cohort_phase_storage"||
        name=="receiver_helper_cohort_phase_applied"||name=="receiver_helper_phase_applied";
    return name=="trace_begin"||name=="receiver_signal_commit"||
        name=="receiver_bank_admitted"||name=="receiver_selection"||name=="receiver_acquisition_work"||name=="receiver_pattern_wire"||
        name.starts_with("receiver_helper_");
}
std::optional<std::uint64_t> number(const std::string& line,std::string_view key) {
    const auto needle='"'+std::string(key)+"\":";const auto at=line.find(needle);
    if(at==std::string::npos)return {};return std::stoull(line.substr(at+needle.size()));
}
std::optional<long double> real_number(const std::string& line,std::string_view key) {
    const auto needle='"'+std::string(key)+"\":";const auto at=line.find(needle);
    if(at==std::string::npos)return {};
    const auto begin=at+needle.size();std::size_t consumed=0;
    const auto value=std::stold(line.substr(begin),&consumed);
    if(!std::isfinite(value)||!consumed||begin+consumed>=line.size()||
       (line[begin+consumed]!=','&&line[begin+consumed]!='}'))return {};
    return value;
}
std::optional<std::string> string_field(const std::string& line,std::string_view key) {
    const auto marker='"'+std::string(key)+"\":\"";const auto at=line.find(marker);
    if(at==std::string::npos)return {};
    const auto begin=at+marker.size(),end=line.find('"',begin);
    if(end==std::string::npos)return {};
    return line.substr(begin,end-begin);
}
std::optional<bool> boolean_field(const std::string& line,std::string_view key) {
    const auto marker='"'+std::string(key)+"\":";const auto at=line.find(marker);
    if(at==std::string::npos)return {};
    const auto rest=std::string_view(line).substr(at+marker.size());
    if(rest.starts_with("true")&&(rest.size()==4||rest[4]==','||rest[4]=='}'))return true;
    if(rest.starts_with("false")&&(rest.size()==5||rest[5]==','||rest[5]=='}'))return false;
    return {};
}
bool trace_integrity(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);std::string first,footer;
    std::array<char,4096> header{};
    bool complete_footer=false;
    if(input&&input.getline(header.data(),static_cast<std::streamsize>(header.size()))) {
        first=header.data();
        input.clear();input.seekg(0,std::ios::end);const auto size=input.tellg();
        if(size>0) {
            const auto count=std::min<std::streamoff>(static_cast<std::streamoff>(size),4096);
            input.seekg(size-count);std::string tail(static_cast<std::size_t>(count),'\0');
            input.read(tail.data(),count);
            if(input&&tail.back()=='\n') {
                tail.pop_back();const auto previous=tail.rfind('\n');
                footer=tail.substr(previous==std::string::npos?0:previous+1);
                complete_footer=footer.starts_with("{\"event\":\"trace_end\",")&&footer.ends_with('}');
            }
        }
    }
    const auto begin_token=string_field(first,"token"),end_token=string_field(footer,"token");
    std::optional<std::uint64_t> dropped;
    try {dropped=number(footer,"dropped");}catch(const std::logic_error&) {} // Malformed evidence is incomplete.
    const auto limited=boolean_field(footer,"size_limited"),failed=boolean_field(footer,"prior_io_failed");
    const bool tokens_match=first.starts_with("{\"event\":\"trace_begin\",")&&begin_token&&end_token&&
        !begin_token->empty()&&*begin_token==*end_token;
    const bool valid=complete_footer&&tokens_match&&dropped&&!*dropped&&
        footer.find("\"dropped\":0,")!=std::string::npos&&limited&&!*limited&&failed&&!*failed;
    std::cout<<"trace_integrity,"<<path.string()<<','<<complete_footer<<','<<tokens_match<<','
        <<(dropped?std::to_string(*dropped):"missing")<<','
        <<(limited?(*limited?"true":"false"):"missing")<<','
        <<(failed?(*failed?"true":"false"):"missing")<<','<<valid<<'\n';
    return valid;
}
struct RowProgress {
    std::uint64_t revision=0,bits=0,first=UINT64_MAX,candidate=0,peak_bits=0,lifetime_bits=0;
    bool initialized=false,pending=false,complete=false,observed_end=false,identity=true,prefix=true;
    bool expected_prefix=true,ever_expected_prefix=false,assisted=false,all_completed_end=true;
    bool lifetime_identity=true,lifetime_prefix=true,lifetime_complete=false;
    std::string raw_prefix,lifetime_raw_prefix;
    void observe(std::uint64_t next_revision,std::uint64_t count,bool completed,bool scored_end,
                 std::optional<std::string_view> observed_prefix,std::string_view expected) {
        // Every completed interpretation retains its own scored-END obligation,
        // even when a later stronger owner changes this row's revision.
        if(completed)all_completed_end=all_completed_end&&scored_end;
        peak_bits=std::max(peak_bits,count);
        lifetime_identity=lifetime_identity&&count>=lifetime_bits&&!(lifetime_complete&&!completed);
        lifetime_bits=count;lifetime_complete=lifetime_complete||completed;
        if(!completed) {
            const bool valid=observed_prefix&&observed_prefix->size()==std::min<std::uint64_t>(count,128)&&
                observed_prefix->find_first_not_of("01")==std::string_view::npos;
            const bool expected_start=valid&&*observed_prefix==expected.substr(0,std::min<std::uint64_t>(count,128));
            ever_expected_prefix=ever_expected_prefix||expected_start;
            expected_prefix=expected_prefix&&expected_start;
            lifetime_prefix=lifetime_prefix&&valid&&observed_prefix->starts_with(lifetime_raw_prefix);
            if(observed_prefix)lifetime_raw_prefix=*observed_prefix;
        }
        if(initialized&&next_revision<revision) {identity=false;return;}
        if(!initialized||next_revision!=revision) {
            // A different stronger hypothesis may reopen a completed row.
            // Revision monotonicity and failures from every old revision stay.
            bits=0;complete=false;observed_end=false;raw_prefix.clear();
            initialized=true;revision=next_revision;
        }
        identity=identity&&count>=bits&&!(complete&&!completed);bits=count;
        if(completed) {complete=true;observed_end=scored_end;}
        else {
            pending=true;
            prefix=prefix&&observed_prefix&&observed_prefix->size()==std::min<std::uint64_t>(count,128)&&
                observed_prefix->find_first_not_of("01")==std::string_view::npos&&observed_prefix->starts_with(raw_prefix);
            if(observed_prefix)raw_prefix=*observed_prefix;
        }
    }
};
void row_progress_guards() {
    constexpr std::string_view expected="11010110";
    const auto pending=[&](RowProgress& row,std::uint64_t revision,std::uint64_t count,std::string_view bits) {
        row.observe(revision,count,false,false,bits,expected);
    };
    const auto completed=[&](RowProgress& row,std::uint64_t revision,std::uint64_t count,bool end=true) {
        row.observe(revision,count,true,end,std::nullopt,expected);
    };
    // Actual secondary row 3: candidate 5, then stronger candidates 14 and 10.
    RowProgress secondary;pending(secondary,3,1,"1");completed(secondary,3,2);
    pending(secondary,10,2,"11");pending(secondary,11,3,"110");completed(secondary,11,3);
    require(secondary.identity&&secondary.prefix&&secondary.all_completed_end&&secondary.complete,
            "valid stronger secondary revisions failed fixture progress guards");
    auto reopened=secondary;pending(reopened,11,3,"110");
    require(!reopened.identity,"same-revision completed owner reopened in fixture guard");
    auto backwards=secondary;pending(backwards,10,3,"110");
    require(!backwards.identity,"backwards row revision escaped fixture guard");
    RowProgress count;pending(count,1,3,"110");pending(count,1,2,"11");
    require(!count.identity,"same-revision count withdrawal escaped fixture guard");
    RowProgress prefix;pending(prefix,1,3,"110");pending(prefix,1,3,"111");
    require(!prefix.prefix,"same-revision prefix withdrawal escaped fixture guard");
    RowProgress primary_count;pending(primary_count,1,3,"110");pending(primary_count,2,2,"11");
    require(primary_count.identity&&!primary_count.lifetime_identity&&primary_count.peak_bits==3,
            "primary count withdrawal across revisions escaped fixture guard");
    RowProgress primary_prefix;pending(primary_prefix,1,3,"110");pending(primary_prefix,2,3,"111");
    require(primary_prefix.prefix&&!primary_prefix.lifetime_prefix&&!primary_prefix.expected_prefix&&
            primary_prefix.ever_expected_prefix,"primary prefix withdrawal across revisions escaped fixture guard");
    RowProgress primary;pending(primary,2,1,"1");pending(primary,5,2,"11");pending(primary,8,8,expected);completed(primary,8,8);
    require(primary.identity&&primary.prefix&&primary.lifetime_identity&&primary.lifetime_prefix&&primary.expected_prefix,
            "valid primary revision changes failed strict lifetime guards");
    pending(primary,9,8,expected);
    require(primary.identity&&!primary.lifetime_identity,"primary reopened across revisions without a lifetime failure");
    RowProgress historical_end;pending(historical_end,3,1,"1");completed(historical_end,3,2,false);
    pending(historical_end,10,2,"11");completed(historical_end,10,2);
    require(!historical_end.all_completed_end,"later revision erased a historical completed-END failure");
    std::cout<<"# row_progress_guards: valid secondary revisions and forbidden reopen/revision/count/prefix/END transitions checked\n";
}
enum CompactCounter : std::size_t {
    projected_samples,lane_samples,skipped_projected_samples,skipped_lane_samples,
    audit_projected_samples,audit_lane_samples,compact_counter_count
};
struct CompactWork {
    std::array<std::uint64_t,compact_counter_count> samples{};
    std::uint64_t instances=0,capability_supported_instances=0;
    bool executed_and_omitted() const noexcept {
        return instances&&samples[lane_samples]&&samples[skipped_lane_samples];
    }
};
enum NativeArithmeticCounter : std::size_t {
    native_fft_jobs,native_direct_jobs,native_exact_reused_jobs,native_partitioned_points,
    native_fp32_transforms,native_fp64_transforms,native_arithmetic_counter_count
};
struct NativeArithmetic {
    std::array<std::uint64_t,native_arithmetic_counter_count> counts{};
    std::uint64_t instances=0;
    bool complete=false;
};
void print_native_arithmetic(const NativeArithmetic& arithmetic) {
    for(const auto count:arithmetic.counts) {
        std::cout<<',';
        if(arithmetic.complete)std::cout<<count;else std::cout<<"unavailable";
    }
}
struct TraceResult {
    bool pending=false,complete=false,observed_end=false,identity=true,prefix=true,assisted=false,phase=false,later_epoch=false;
    bool all_rows_end=true;
    std::uint64_t id=0,revision=0,bits=0;
    std::uint64_t capture_revision=0; // Diagnostic join only; never completion authority.
    std::uint64_t rows=0,secondary_rows=0;
    bool wire_exact=false,wire_contiguous=false,wire_complete=false;
    std::uint64_t wire_bits=0;
    std::uint64_t work_instances=0,template_jobs=0,input_transforms=0,conditional_jobs=0,audit_jobs=0;
    std::uint64_t unique_conditional_jobs=0,input_view_jobs=0,input_view_instances=0,input_view_admissions=0;
    bool primary_input_view_certified=false,later_input_view_assisted=false;
    bool realtime_primary_qualified=false,realtime_later_qualified=false,realtime_trace_present=false;
    std::uint64_t realtime_qualified_instances=0;
    CompactWork compact{};
    NativeArithmetic native_arithmetic{};
    bool frequency_narrowed=false,timing=false,later_assisted_epoch=false;
    std::set<std::array<std::uint64_t,3>> narrowed_instances;
    bool cohort_frontend=false,cohort_phase=false,cohort_resources_valid=true;
    std::uint64_t cohort_peak_bytes=0,cohort_reserve_bytes=0;
    bool trace_complete=false;
    bool wire_evidence_complete=true,wire_assisted=false;
    std::set<std::string> selection_backends;
};
TraceResult inspect_trace(const std::filesystem::path& path,const Bytes& expected,const Bytes& dated_wire,const modem::Config& config,
                          const WireCollector* collector=nullptr,const modem::OscillatorPatternSearch* ordinary_baseline=nullptr) {
    const auto rate=config.sample_rate;
    TraceResult result;result.wire_evidence_complete=!collector||collector->complete();
    result.trace_complete=trace_integrity(path);
    // Missing trace data cannot establish payload errors, progress or END.
    // No incomplete evidence is used for qualification or work accounting.
    if(!result.trace_complete)return result;
    std::ifstream input(path);std::string line;std::set<std::uint64_t> initial_epochs;
    std::map<std::array<std::uint64_t,3>,std::array<std::uint64_t,4>> work;
    std::map<std::array<std::uint64_t,3>,std::array<std::uint64_t,compact_counter_count>> compact_work;
    std::set<std::array<std::uint64_t,3>> compact_capability_supported;
    std::map<std::array<std::uint64_t,3>,std::array<std::uint64_t,native_arithmetic_counter_count>> native_arithmetic;
    bool native_arithmetic_missing=false;
    struct RealtimeWork {
        std::uint64_t epoch=0,family=0,sample=0,original=0,selected=0,last_native=0,last_skipped=0,last_lanes=0,last_skipped_lanes=0;
        bool qualified_execution=false,actual_omission=false,clock_qualified=false;
        long double unresolved_clock_cell_half_width_ppm=0;
    };
    std::map<std::array<std::uint64_t,3>,RealtimeWork> realtime_work;
    // Selection is pre-constructor geometry, not executed work. It is used
    // only with a joined numeric owner and same physical modem geometry.
    std::map<std::array<std::uint64_t,3>,std::set<std::uint64_t>> realtime_selections;
    struct InputViewAdmission {
        std::uint64_t epoch=0,family=0,view=0,sample=0,original=0,selected=0;
        std::uint64_t raw_reference_sample=0,raw_frequency_sample=0,physical_rate=0,captured_ingress=0,source_origin=0;
        long double frequency=0,uncertainty=0,correction=0,clock_lower=0,clock_upper=0;
        bool conjugate=false,later=false;
    };
    struct InputViewWork {
        std::uint64_t epoch=0,family=0,view=0,jobs=0;
    };
    std::map<std::array<std::uint64_t,3>,InputViewAdmission> input_view_admissions;
    std::map<std::array<std::uint64_t,3>,InputViewWork> input_view_work;
    std::map<std::array<std::uint64_t,3>,std::uint64_t> input_view_identities;
    const auto declared_clock_half_width=config.oscillator_search?
        std::optional<double>{modem::oscillator_pattern_search(config).requested_clock_half_width_ppm}:std::nullopt;
    std::map<std::array<std::uint64_t,2>,RowProgress> rows;
    struct Wire {
        std::uint64_t instance=0,first_sample=0,first_symbol=0,epoch=0,family=0,origin=0;
        bool initialized=false,contiguous=true,consistent=true,complete=false,observed_end=false,helper_acquired=false;
        bool helper_conditioned_acquisition=false;
        std::string bits;
    };
    std::map<std::array<std::uint64_t,2>,Wire> wires;
    constexpr std::size_t trace_bit_capacity=32*1024*1024;
    std::size_t trace_bits=0;
    const auto expected_text=binary(expected),expected_wire=binary(dated_wire);
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_acquisition_work_unavailable\"")!=std::string::npos)
            native_arithmetic_missing=true;
        if(line.find("\"event\":\"receiver_selection\"")!=std::string::npos)
            if(const auto backend=string_field(line,"backend"))result.selection_backends.insert(*backend);
        if(line.find("\"event\":\"receiver_selection\"")!=std::string::npos) {
            const auto revision=number(line,"revision"),epoch=number(line,"epoch"),family=number(line,"family"),pairs=number(line,"frequency_rate_pairs");
            if(revision&&epoch&&family&&pairs&&number(line,"sample_rate")==config.sample_rate&&
               number(line,"symbol_samples")==modem::symbol_sample_count(config)&&
               number(line,"fine_chip_samples")==modem::pattern_chip_samples(config)&&number(line,"dsss_factor")==config.dsss_factor)
                realtime_selections[{*revision,*epoch,*family}].insert(*pairs);
        }
        const bool frontend=line.find("\"event\":\"receiver_helper_cohort_frontend\"")!=std::string::npos;
        const bool phase_storage=line.find("\"event\":\"receiver_helper_cohort_phase_storage\"")!=std::string::npos;
        if(frontend||phase_storage) {
            const auto actual=number(line,"actual_bytes"),reserved=number(line,"reserved_bytes");
            require(actual&&reserved,"helper cohort trace omitted its actual allocation or reserve");
            result.cohort_frontend=result.cohort_frontend||frontend;
            result.cohort_phase=result.cohort_phase||phase_storage;
            const bool typed_mode_grant=*reserved==modem::SampledSimulationChannel::helper_cohort_workspace_bound||
                *reserved==modem::SampledSimulationChannel::physical_helper_cohort_workspace_bound;
            result.cohort_resources_valid=result.cohort_resources_valid&&*actual<=*reserved&&
                typed_mode_grant;
            result.cohort_peak_bytes=std::max(result.cohort_peak_bytes,*actual);
            result.cohort_reserve_bytes=result.cohort_reserve_bytes?std::min(result.cohort_reserve_bytes,*reserved):*reserved;
        }
        if(line.find("\"event\":\"receiver_pattern_wire\"")!=std::string::npos) {
            const auto capture=number(line,"input_revision"),candidate=number(line,"candidate_id"),
                instance=number(line,"instance_id"),epoch=number(line,"epoch"),first_sample=number(line,"stream_first_sample"),
                first_symbol=number(line,"stream_first_symbol"),ordinal=number(line,"first_stream_symbol"),missing=number(line,"missing_slots"),
                family=number(line,"family"),origin=number(line,"sample_origin");
            auto bits=string_field(line,"bits");
            require(capture&&candidate&&instance&&epoch&&first_sample&&first_symbol&&ordinal&&missing&&bits&&family&&origin,
                    "physical wire trace omitted its actual stream identity, ordinal or bits");
            require(bits->size()<=1024&&bits->find_first_not_of("012")==std::string::npos&&
                    (!*missing||bits->empty()),"physical wire trace violated its bounded piece contract");
            auto& wire=wires[{*capture,*candidate}];
            if(!wire.initialized) {
                wire.initialized=true;wire.instance=*instance;wire.epoch=*epoch;
                wire.first_sample=*first_sample;wire.first_symbol=*first_symbol;wire.family=*family;wire.origin=*origin;
            }
            require(wire.instance==*instance&&wire.epoch==*epoch&&wire.first_sample==*first_sample&&
                    wire.first_symbol==*first_symbol&&wire.family==*family&&wire.origin==*origin&&*ordinal>=*first_symbol,
                    "physical wire candidate changed its instance or stable stream coordinates");
            const auto relative=*ordinal-*first_symbol;
            require(relative<=trace_bit_capacity&&*missing<=trace_bit_capacity,"physical wire observation exceeds the test evidence bound");
            if(*missing)bits->assign(static_cast<std::size_t>(*missing),'2');
            require(bits->size()<=trace_bit_capacity-static_cast<std::size_t>(relative),"physical wire ordinal exceeds the test evidence bound");
            if(wire.complete&&relative+bits->size()>wire.bits.size())wire.consistent=false;
            if(relative>wire.bits.size()) {
                const auto gap=static_cast<std::size_t>(relative)-wire.bits.size();
                require(gap<=trace_bit_capacity-trace_bits,"physical wire evidence exceeds its test-only memory bound");
                wire.bits.append(gap,'2');trace_bits+=gap;wire.contiguous=false;
            }
            for(std::size_t i=0;i<bits->size();++i) {
                const auto offset=static_cast<std::size_t>(relative)+i;
                if(offset<wire.bits.size())wire.consistent=wire.consistent&&wire.bits[offset]==(*bits)[i];
                else {
                    require(trace_bits<trace_bit_capacity,"physical wire evidence exceeds its test-only memory bound");
                    wire.bits.push_back((*bits)[i]);++trace_bits;
                }
            }
            if(line.find("\"complete\":1")!=std::string::npos) {
                wire.complete=true;const auto end=number(line,"physical_end"),feed=number(line,"feed_end_sample");
                wire.observed_end=end&&feed&&*feed>=*end+6ULL*rate;
            }
            if(const auto assisted=number(line,"helper_acquired"))wire.helper_acquired=wire.helper_acquired||*assisted;
            if(const auto conditioned=number(line,"helper_conditioned_acquisition"))
                wire.helper_conditioned_acquisition=wire.helper_conditioned_acquisition||*conditioned;
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
            require(!number(line,"work_counter_regressed").value_or(0),
                    "sampled acquisition work counters reset between published snapshots");
            const auto revision=number(line,"revision"),id=number(line,"instance_id"),origin=number(line,"sample_origin");
            const auto jobs=number(line,"template_jobs"),transforms=number(line,"input_transforms"),
                conditional=number(line,"conditional_jobs"),audit=number(line,"audit_jobs");
            require(revision&&id&&origin&&jobs&&transforms&&conditional&&audit,
                    "ordinary acquisition work trace omitted an identity or counter");
            auto& previous=work[{*revision,*id,*origin}];
            const std::array<std::uint64_t,4> current{*jobs,*transforms,*conditional,*audit};
            for(unsigned i=0;i<current.size();++i) {
                require(current[i]>=previous[i],"ordinary acquisition work counters reset within one traced instance");
                previous[i]=current[i];
            }
            const auto view=number(line,"helper_input_view").value_or(0);
            const std::array identity{*revision,*id,*origin};
            constexpr std::array<std::string_view,native_arithmetic_counter_count> arithmetic_fields{
                "fft_jobs","direct_jobs","exact_reused_template_jobs","partitioned_transform_points",
                "native_fp32_transforms","tabulated_fp64_transforms"};
            std::array<std::optional<std::uint64_t>,native_arithmetic_counter_count> arithmetic;
            std::size_t arithmetic_present=0;
            for(std::size_t i=0;i<arithmetic.size();++i) {
                arithmetic[i]=number(line,arithmetic_fields[i]);arithmetic_present+=arithmetic[i].has_value();
            }
            if(arithmetic_present) {
                require(arithmetic_present==arithmetic.size(),"native arithmetic trace omitted part of its counter schema");
                auto& prior=native_arithmetic[identity];
                for(std::size_t i=0;i<arithmetic.size();++i) {
                    require(*arithmetic[i]>=prior[i],"native arithmetic counters reset within one traced instance");
                    prior[i]=*arithmetic[i];
                }
            } else native_arithmetic_missing=true; // Historical traces cannot establish zero arithmetic.
            if(number(line,"realtime_helper").value_or(0)) {
                result.realtime_trace_present=true;
                const auto locked=number(line,"realtime_locked"),supported=number(line,"helper_window_supported"),
                    active=number(line,"helper_window_active"),original=number(line,"helper_window_original_pairs"),
                    selected=number(line,"helper_window_selected_pairs"),epoch=number(line,"epoch"),family=number(line,"family"),
                    fft=number(line,"fft_jobs"),direct=number(line,"direct_jobs"),skipped=number(line,"skipped_template_jobs"),
                    lanes=number(line,"accumulated_lane_samples"),skipped_lanes=number(line,"skipped_lane_samples");
                const auto uncertainty=real_number(line,"realtime_reference_uncertainty_hz"),
                    unresolved_clock=real_number(line,"helper_window_unresolved_clock_cell_half_width_ppm");
                const auto clock_qualified=number(line,"helper_window_clock_qualified");
                // Missing additive schema is unavailable evidence, never zero
                // work or implied lock. Original payload parsing still runs.
                if(locked&&supported&&active&&original&&selected&&epoch&&family&&fft&&direct&&skipped&&lanes&&skipped_lanes&&uncertainty&&unresolved_clock&&clock_qualified) {
                    auto& prior=realtime_work[identity];
                    require(*fft<=UINT64_MAX-*direct,"ordinary native work counter overflowed");
                    const auto native=*fft+*direct;
                    require(native>=prior.last_native&&*skipped>=prior.last_skipped&&*lanes>=prior.last_lanes&&*skipped_lanes>=prior.last_skipped_lanes,
                        "realtime ordinary work reset within one owner");
                    const bool executed=native>prior.last_native||*lanes>prior.last_lanes;
                    const bool omitted=*skipped>prior.last_skipped||*skipped_lanes>prior.last_skipped_lanes;
                    const bool qualified=*locked&&*supported&&*active&&*original&&*selected&&*selected<=*original&&*uncertainty>=0;
                    prior.qualified_execution|=qualified&&executed;
                    prior.actual_omission|=qualified&&executed&&omitted&&*selected<*original;
                    if(qualified&&executed) {prior.epoch=*epoch;prior.family=*family;prior.sample=number(line,"sample").value_or(0);prior.original=*original;prior.selected=*selected;
                        prior.clock_qualified=*clock_qualified!=0;prior.unresolved_clock_cell_half_width_ppm=*unresolved_clock;}
                    prior.last_native=native;prior.last_skipped=*skipped;prior.last_lanes=*lanes;prior.last_skipped_lanes=*skipped_lanes;
                }
            }
            const auto [view_identity,inserted]=input_view_identities.emplace(identity,view);
            require(inserted||view_identity->second==view,"ordinary receiver changed its immutable input-view identity");
            if(view) {
                const auto view_jobs=number(line,"input_view_conditional_jobs"),domain=number(line,"input_view_conditional_domain"),
                    epoch=number(line,"epoch"),family=number(line,"family");
                require(view_jobs&&domain&&*domain&&epoch&&family&&*view_jobs==*jobs&&*conditional<=*view_jobs,
                        "conditional input-view work lacks its actual native jobs or ordinary row identity");
                auto [found,created]=input_view_work.emplace(identity,InputViewWork{*epoch,*family,view,*view_jobs});
                require(created||(found->second.epoch==*epoch&&found->second.family==*family&&
                        found->second.view==view&&*view_jobs>=found->second.jobs),
                        "conditional input-view work changed identity or reset its execution count");
                found->second.jobs=*view_jobs;
            }
            if(const auto compact=number(line,"compact_supported");compact) {
                constexpr std::array<std::string_view,compact_counter_count> fields{
                    "projected_carrier_samples","accumulated_lane_samples","skipped_carrier_samples",
                    "skipped_lane_samples","broad_audit_projected_carrier_samples","broad_audit_accumulated_lane_samples"};
                const std::array identity{*revision,*id,*origin};
                if(*compact)compact_capability_supported.insert(identity);
                std::array<std::uint64_t,compact_counter_count> current{};
                for(std::size_t i=0;i<fields.size();++i) {
                    const auto counter=number(line,fields[i]);
                    require(counter.has_value(),"compact acquisition work trace omitted an executed sample counter");
                    current[i]=*counter;
                }
                // Capability describes a particular assisted feature. Ordinary
                // compact arithmetic can execute even when that feature was
                // declined; preserve its actual counters without qualifying it.
                if(*compact || std::any_of(current.begin(),current.end(),[](auto value){return value!=0;}) ||
                   compact_work.contains(identity)) {
                    auto& prior=compact_work[identity];
                    for(std::size_t i=0;i<fields.size();++i) {
                        const auto counter=current[i];
                        require(counter>=prior[i],"compact acquisition work counters reset within one traced instance");
                        prior[i]=counter;
                    }
                    require(prior[audit_projected_samples]<=prior[projected_samples]&&
                            prior[audit_lane_samples]<=prior[lane_samples],
                            "compact audit work exceeds total executed sample work");
                }
            }
        }
        if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
            const auto epoch=number(line,"epoch"),sample=number(line,"sample");
            if(epoch&&sample) {if(*sample==0)initial_epochs.insert(*epoch);else if(!initial_epochs.contains(*epoch))result.later_epoch=true;}
        }
        if(line.find("\"event\":\"receiver_helper_input_view_admission\"")!=std::string::npos) {
            // Prepared proposals cannot qualify. This record is emitted only
            // after the ordinary receiver's constructor/staging commit.
            const auto admitted=number(line,"admitted"),qualified=number(line,"qualified_frequency"),
                revision=number(line,"revision"),id=number(line,"instance_id"),origin=number(line,"sample_origin"),
                epoch=number(line,"epoch"),family=number(line,"family"),view=number(line,"view_id"),sample=number(line,"sample"),
                original=number(line,"original_pairs"),selected=number(line,"selected_pairs"),
                raw_reference=number(line,"raw_reference_sample"),raw_frequency=number(line,"raw_frequency_sample"),
                physical_rate=number(line,"physical_sample_rate"),ingress=number(line,"captured_ingress"),
                source_origin=number(line,"helper_source_origin"),conjugate=number(line,"conjugate");
            const auto frequency=real_number(line,"raw_frequency_hz"),uncertainty=real_number(line,"raw_frequency_uncertainty_hz"),
                correction=real_number(line,"correction_hz"),clock_lower=real_number(line,"qualified_clock_lower_ppm"),
                clock_upper=real_number(line,"qualified_clock_upper_ppm");
            require(admitted&&*admitted&&qualified&&*qualified&&revision&&id&&origin&&epoch&&family&&view&&
                    *view>0&&*view<=2&&sample&&original&&selected&&*selected>0&&*selected<*original&&
                    raw_reference&&raw_frequency&&physical_rate&&*physical_rate>0&&*physical_rate<=120000000&&
                    ingress&&source_origin&&conjugate&&*conjugate<=1&&frequency&&uncertainty&&correction&&clock_lower&&clock_upper,
                    "committed helper input view omitted its qualified birth certificate");
            const auto frontier=static_cast<long double>(*source_origin)+std::max(*sample,*ingress);
            const auto reference_date=static_cast<long double>(*raw_reference)*rate/static_cast<long double>(*physical_rate);
            const auto frequency_date=static_cast<long double>(*raw_frequency)*rate/static_cast<long double>(*physical_rate);
            const auto clock_bound=declared_clock_half_width?std::nextafter(*declared_clock_half_width,
                std::numeric_limits<double>::infinity()):0.;
            require(declared_clock_half_width&&*raw_frequency<=*raw_reference&&reference_date<=frontier&&
                    frequency_date<=frontier&&frontier-frequency_date<=rate&&*uncertainty>0&&
                    *frequency-*uncertainty>0&&*frequency+*uncertainty<static_cast<long double>(*physical_rate)/2&&
                    *clock_lower<=*clock_upper&&*clock_lower>=-clock_bound&&*clock_upper<=clock_bound,
                    "committed helper input view lacks causal fresh frequency or a valid declared clock interval");
            InputViewAdmission certificate;
            certificate.epoch=*epoch;certificate.family=*family;certificate.view=*view;certificate.sample=*sample;
            certificate.original=*original;certificate.selected=*selected;
            certificate.raw_reference_sample=*raw_reference;certificate.raw_frequency_sample=*raw_frequency;
            certificate.physical_rate=*physical_rate;certificate.captured_ingress=*ingress;certificate.source_origin=*source_origin;
            certificate.frequency=*frequency;certificate.uncertainty=*uncertainty;certificate.correction=*correction;
            certificate.clock_lower=*clock_lower;certificate.clock_upper=*clock_upper;certificate.conjugate=*conjugate!=0;
            certificate.later=*sample&&!initial_epochs.contains(*epoch);
            const auto [entry,inserted]=input_view_admissions.emplace(std::array{*revision,*id,*origin},certificate);
            (void)entry;
            require(inserted,"ordinary receiver committed the same input-view birth more than once");
        }
        if(line.find("\"event\":\"receiver_helper_cohort_dispatch\"")!=std::string::npos) {
            const auto qualified=number(line,"qualified"),conditional=number(line,"conditional"),
                original=number(line,"original_pairs"),selected=number(line,"selected_pairs");
            result.frequency_narrowed=result.frequency_narrowed||(qualified&&*qualified&&conditional&&*conditional&&
                original&&selected&&*selected>0&&*selected<*original);
        }
        const bool compact_prior=line.find("\"event\":\"receiver_helper_compact_work\"")!=std::string::npos;
        const bool native_prior=line.find("\"event\":\"receiver_helper_cohort_prior_decision\"")!=std::string::npos;
        if(compact_prior||native_prior) {
            const auto original=number(line,"original_pairs"),selected=number(line,"selected_pairs");
            bool narrowed=original&&selected&&*selected>0&&*selected<*original;
            if(compact_prior) {
                const auto skipped=number(line,"skipped_lane_samples"),active=number(line,"active_pairs"),
                    applied=number(line,"prior_active");
                narrowed=narrowed&&applied&&*applied&&skipped&&*skipped&&active&&*active<*original;
                if(const auto timing=number(line,"timing_narrowing_active"))result.timing=result.timing||*timing;
            } else {
                const auto admitted=number(line,"admitted"),active=number(line,"prior_active");
                narrowed=narrowed&&admitted&&*admitted&&active&&*active;
            }
            if(narrowed) {
                const auto revision=number(line,"revision"),id=number(line,"instance_id"),origin=number(line,"sample_origin"),
                    epoch=number(line,"epoch"),sample=number(line,"sample");
                require(revision&&id&&origin&&epoch&&sample,"helper prior trace omitted its ordinary receiver identity");
                result.narrowed_instances.insert({*revision,*id,*origin});
                if(*sample&&!initial_epochs.contains(*epoch))result.later_assisted_epoch=true;
            }
        }
        result.phase=result.phase||line.find("\"event\":\"receiver_helper_phase_applied\"")!=std::string::npos||
            line.find("\"event\":\"receiver_helper_cohort_phase_applied\"")!=std::string::npos;
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        const auto count=number(line,"received_bits");if(!count||!*count)continue;
        const auto capture=number(line,"input_revision"),id=number(line,"signal_id"),revision=number(line,"revision"),
            candidate=number(line,"candidate_id");
        require(capture&&id&&revision&&candidate,"selected receiver trace omitted capture, row/candidate identity or revision");
        auto& row=rows[{*capture,*id}];
        row.candidate=*candidate;
        if(const auto first=number(line,"physical_first"))row.first=std::min(row.first,*first);
        row.assisted=row.assisted||line.find("\"helper_assisted\":1")!=std::string::npos;
        const bool completed=line.find("\"complete\":1")!=std::string::npos;
        const auto end=number(line,"physical_end"),feed=number(line,"feed_end_sample");
        const bool scored_end=end&&feed&&*feed>=*end&&*feed-*end>=6ULL*rate;
        const auto prefix=string_field(line,"raw_bits_prefix");
        row.observe(*revision,*count,completed,scored_end,prefix?std::optional<std::string_view>(*prefix):std::nullopt,expected_text);
    }
    // The known transmitted sequence identifies a test subject; it is never
    // supplied to the receiver. Other rolling epochs may independently expose
    // valid suffixes with different row IDs and physical starts.
    const RowProgress* primary=nullptr;std::array<std::uint64_t,2> primary_key{};
    std::optional<std::array<std::uint64_t,3>> primary_numeric_key;
    std::uint64_t primary_epoch=0,primary_family=0;
    for(const auto& [key,row]:rows) {
        result.identity=result.identity&&row.identity;
        result.prefix=result.prefix&&row.prefix;
        result.all_rows_end=result.all_rows_end&&row.all_completed_end;
        // Keep a once-identified primary eligible after a bad withdrawal so
        // a new revision cannot hide its count/prefix failure by disappearing.
        if(!row.pending||!row.ever_expected_prefix)continue;
        if(!primary||row.peak_bits>primary->peak_bits||(row.peak_bits==primary->peak_bits&&row.first<primary->first)) {
            primary=&row;primary_key=key;
        }
    }
    result.rows=rows.size();result.secondary_rows=rows.size()-(primary?1:0);
    if(primary) {
        result.identity=result.identity&&primary->lifetime_identity;
        result.prefix=result.prefix&&primary->lifetime_prefix&&primary->expected_prefix;
        result.id=primary_key[1];result.revision=primary->revision;result.bits=primary->bits;
        result.capture_revision=primary_key[0];
        result.pending=primary->pending;result.complete=primary->complete&&primary->bits==expected.size();
        result.observed_end=primary->observed_end;result.assisted=primary->assisted;
        if(collector) {
            if(const auto* wire=collector->find(primary_key[0],primary->candidate)) {
                primary_numeric_key=std::array{primary_key[0],wire->instance,wire->origin};
                primary_epoch=wire->epoch;primary_family=wire->family;
                result.wire_bits=wire->size;
                result.wire_exact=wire->size==dated_wire.size()&&
                    (!wire->size||std::equal(wire->bits.get(),wire->bits.get()+wire->size,dated_wire.begin()));
                result.wire_contiguous=wire->contiguous&&wire->consistent;
                result.wire_complete=wire->complete&&wire->observed_end;
                result.wire_assisted=wire->helper_acquired||wire->helper_conditioned_acquisition;
            }
        } else if(const auto found=wires.find({primary_key[0],primary->candidate});found!=wires.end()) {
            const auto& wire=found->second;result.wire_bits=wire.bits.size();
            primary_numeric_key=std::array{primary_key[0],wire.instance,wire.origin};
            primary_epoch=wire.epoch;primary_family=wire.family;
            result.wire_exact=wire.bits==expected_wire;
            result.wire_contiguous=wire.contiguous&&wire.consistent;
            result.wire_complete=wire.complete&&wire.observed_end;
            result.wire_assisted=wire.helper_acquired||wire.helper_conditioned_acquisition;
        }
    }
    // Only the selected primary wire can supply this actual birth evidence.
    // The GUI/history helper policy remains independent and unchanged.
    result.assisted=result.assisted||result.wire_assisted;
    std::cout<<"helper_resource_trace,"<<path.string()<<','<<result.cohort_frontend<<','<<result.cohort_phase<<','
        <<result.cohort_peak_bytes<<','<<result.cohort_reserve_bytes<<','<<result.cohort_resources_valid<<'\n';
    for(const auto& [key,row]:rows)
        std::cout<<"trace_row,"<<path.string()<<','<<key[0]<<','<<key[1]<<','<<row.revision<<','<<row.first<<','
            <<row.bits<<','<<row.pending<<','<<row.complete<<','<<row.observed_end<<','<<row.identity<<','<<row.prefix<<','
            <<row.expected_prefix<<','<<(primary&&key==primary_key)<<','<<row.assisted<<'\n';
    for(const auto& [key,wire]:wires)
        std::cout<<"trace_wire,"<<path.string()<<','<<key[0]<<','<<key[1]<<','<<wire.instance<<','<<wire.first_sample<<','
            <<wire.first_symbol<<','<<wire.bits.size()<<','<<wire.contiguous<<','<<wire.consistent<<','<<wire.complete<<','
            <<wire.observed_end<<','<<(wire.bits==expected_wire)<<','
            <<(primary&&key[0]==primary_key[0]&&key[1]==primary->candidate)<<'\n';
    if(collector)for(const auto& wire:collector->candidates()) {
        const bool equal=wire.size==dated_wire.size()&&
            (!wire.size||std::equal(wire.bits.get(),wire.bits.get()+wire.size,dated_wire.begin()));
        std::cout<<"trace_wire,"<<path.string()<<','<<wire.capture<<','<<wire.candidate<<','<<wire.instance<<','<<wire.first_sample<<','
            <<wire.first_symbol<<','<<wire.size<<','<<wire.contiguous<<','<<wire.consistent<<','<<wire.complete<<','
            <<wire.observed_end<<','<<equal<<','
            <<(primary&&wire.capture==primary_key[0]&&wire.candidate==primary->candidate)<<'\n';
    }
    result.work_instances=work.size();
    for(const auto& entry:work) {
        const auto& counters=entry.second;
        result.template_jobs+=counters[0];result.input_transforms+=counters[1];
        result.conditional_jobs+=counters[2];result.audit_jobs+=counters[3];
        const auto view=input_view_work.find(entry.first);
        // A view's complete ordinary job count already includes any later
        // original-mask conditional work inside that constructed catalogue.
        // Account for those jobs once, never add them to template_jobs again.
        const auto conditional=view==input_view_work.end()?counters[2]:view->second.jobs;
        require(conditional<=UINT64_MAX-result.unique_conditional_jobs,"conditional native work overflowed");
        result.unique_conditional_jobs+=conditional;
    }
    result.compact.instances=compact_work.size();
    result.compact.capability_supported_instances=compact_capability_supported.size();
    for(const auto& [identity,counters]:compact_work) {
        (void)identity;
        for(std::size_t i=0;i<counters.size();++i) {
            require(counters[i]<=UINT64_MAX-result.compact.samples[i],"compact aggregate sample work overflowed");
            result.compact.samples[i]+=counters[i];
        }
    }
    result.native_arithmetic.instances=native_arithmetic.size();
    result.native_arithmetic.complete=!native_arithmetic_missing&&!work.empty()&&native_arithmetic.size()==work.size();
    for(const auto& [identity,counters]:native_arithmetic) {
        (void)identity;
        for(std::size_t i=0;i<counters.size();++i) {
            require(counters[i]<=UINT64_MAX-result.native_arithmetic.counts[i],"native arithmetic counter aggregate overflowed");
            result.native_arithmetic.counts[i]+=counters[i];
        }
    }
    result.input_view_admissions=input_view_admissions.size();
    for(const auto& [identity,executed]:input_view_work) {
        if(!executed.jobs)continue;
        const auto found=input_view_admissions.find(identity);
        require(found!=input_view_admissions.end(),"executed helper input view has no committed qualified birth certificate");
        const auto& certificate=found->second;
        require(certificate.view==executed.view&&certificate.epoch==executed.epoch&&certificate.family==executed.family,
                "executed helper input view does not match its committed ordinary receiver birth");
        require(executed.jobs<=UINT64_MAX-result.input_view_jobs,"conditional input-view native jobs overflowed");
        result.input_view_jobs+=executed.jobs;++result.input_view_instances;
        result.frequency_narrowed=true;result.narrowed_instances.insert(identity);
        result.later_input_view_assisted=result.later_input_view_assisted||certificate.later;
        result.later_assisted_epoch=result.later_assisted_epoch||certificate.later;
        if(primary_numeric_key&&identity==*primary_numeric_key) {
            require(primary_epoch==certificate.epoch&&primary_family==certificate.family,
                    "primary wire changed the qualified input-view birth identity");
            result.primary_input_view_certified=true;
        }
    }
    for(const auto& [identity,certificate]:input_view_admissions) {
        const auto executed=input_view_work.find(identity);
        std::cout<<std::setprecision(17)<<"input_view_admission,"<<path.string()<<','<<identity[0]<<','<<identity[1]<<','<<identity[2]<<','
            <<certificate.epoch<<','<<certificate.family<<','<<certificate.view<<','<<certificate.original<<','<<certificate.selected<<','
            <<certificate.frequency<<','<<certificate.uncertainty<<','<<certificate.raw_reference_sample<<','
            <<certificate.raw_frequency_sample<<','<<certificate.physical_rate<<','<<certificate.captured_ingress<<','
            <<certificate.source_origin<<','<<certificate.correction<<','<<certificate.conjugate<<','
            <<certificate.clock_lower<<','<<certificate.clock_upper<<','<<certificate.later<<','
            <<(executed==input_view_work.end()?0:executed->second.jobs)<<','
            <<(result.primary_input_view_certified&&primary_numeric_key&&identity==*primary_numeric_key)<<'\n';
    }
    for(const auto& [identity,executed]:realtime_work) {
        const auto selected=realtime_selections.find({identity[0],executed.epoch,executed.family});
        const bool matched_geometry=selected!=realtime_selections.end()&&selected->second.contains(executed.original);
        const bool conditional_birth=ordinary_baseline&&executed.original>0&&executed.original<ordinary_baseline->hypotheses.size();
        const bool qualified=executed.qualified_execution&&matched_geometry&&(conditional_birth||executed.actual_omission);
        result.realtime_qualified_instances+=qualified;
        result.realtime_later_qualified|=qualified&&executed.sample&&!initial_epochs.contains(executed.epoch);
        const bool primary_owner=primary_numeric_key&&identity==*primary_numeric_key&&
            executed.epoch==primary_epoch&&executed.family==primary_family;
        result.realtime_primary_qualified|=qualified&&primary_owner;
        std::cout<<"realtime_work_provenance,"<<path.string()<<','<<identity[0]<<','<<identity[1]<<','<<identity[2]<<','
            <<executed.epoch<<','<<executed.family<<','<<executed.original<<','<<executed.selected<<','
            <<executed.clock_qualified<<','<<executed.unresolved_clock_cell_half_width_ppm<<',';
        if(ordinary_baseline)std::cout<<ordinary_baseline->hypotheses.size();else std::cout<<"unavailable";
        std::cout<<','<<matched_geometry<<','<<executed.qualified_execution<<','<<conditional_birth<<','<<executed.actual_omission<<','
            <<qualified<<','<<primary_owner<<'\n';
    }
    require(result.input_view_jobs<=result.template_jobs&&result.unique_conditional_jobs<=result.template_jobs,
            "conditional view accounting counted native execution more than once");
    return result;
}
struct DecisionTiming {
    std::uint64_t decisions=0,availability_joins=0,publication_joins=0,replay_publications=0;
    double maximum_lookahead_seconds=0,maximum_availability_wall_seconds=0,maximum_publication_wall_seconds=0;
};
DecisionTiming decision_timing(const RuntimeMetrics& metrics,const std::filesystem::path& path,const TraceResult& primary) {
    // Join actual completed input blocks and the receiver-selected GUI row.
    // The test's expected wire length never enters this measurement.
    std::map<std::pair<std::uint64_t,std::uint64_t>,const RuntimeMetrics::Feed*> availability;
    for(std::size_t i=0;i<metrics.feed_count;++i) {
        const auto& feed=metrics.feeds[i];
        if(feed.bank_end>feed.bank_begin)availability.emplace(std::pair{feed.revision,feed.bank_end},&feed);
    }
    DecisionTiming result;std::uint64_t bits=0;
    for(std::ifstream input(path);input;) {
        std::string line;if(!std::getline(input,line))break;
        if(line.find("\"event\":\"receiver_signal_commit\"")==std::string::npos)continue;
        const auto capture=number(line,"input_revision"),id=number(line,"signal_id"),revision=number(line,"revision"),count=number(line,"received_bits"),
            end=number(line,"physical_end"),frontier=number(line,"feed_end_sample");
        const auto when=real_number(line,"steady");
        if(!capture||*capture!=primary.capture_revision||!id||*id!=primary.id||!revision||!count||*count<=bits||!end||!frontier||!when)continue;
        bits=*count;++result.decisions;
        if(*frontier>=*end)result.maximum_lookahead_seconds=std::max(result.maximum_lookahead_seconds,
            static_cast<double>(*frontier-*end)/metrics.rate);
        const auto found=availability.lower_bound({*capture,*end});
        if(found!=availability.end()&&found->first.first==*capture&&found->second->bank_begin<*end&&
           found->second->bank_end>=*end&&*when>=found->second->feed.wall) {
            ++result.availability_joins;
            result.maximum_availability_wall_seconds=std::max(result.maximum_availability_wall_seconds,
                static_cast<double>(*when-found->second->feed.wall));
        }
        for(std::size_t i=0;i<metrics.poll_count;++i) {
            const auto& poll=metrics.polls[i];
            if(poll.capture!=*capture||poll.id!=*id||poll.revision<*revision||poll.bits<*count||poll.time.wall<*when)continue;
            ++result.publication_joins;result.replay_publications+=poll.replay;
            result.maximum_publication_wall_seconds=std::max(result.maximum_publication_wall_seconds,
                static_cast<double>(poll.time.wall-*when));break;
        }
    }
    return result;
}
void report_tx_epoch_diagnostic(Profile profile,bool helper,const Resolved& resolved,const modem::Config& transmitted,
        std::uint64_t tx_epoch,const Bytes& expected_wire,const TraceResult& primary,const std::filesystem::path& path,
        const WireCollector* collector=nullptr) {
    // POSTHOC ONLY: TX's actual observed epoch identifies diagnostic subjects.
    // Nothing here selects the general prefix subject or enters any receiver,
    // source decoder, framing, recovery, allocation or completion decision.
    const auto control=resolved.gpsdo_reference?"gpsdo-simulation-reference":"declared";
    const auto family=resolved.settings.transfer.key?
        std::optional<std::uint64_t>{resolved.settings.permits_plaintext()?1ULL:0ULL}:std::nullopt;
    std::optional<std::uint64_t> primary_first_epoch,primary_last_epoch;bool primary_mixed_epochs=false;
    std::map<std::array<std::uint64_t,4>,std::string> final_records;
    constexpr std::array<std::string_view,6> joined_events{"receiver_acquisition_work",
        "receiver_helper_cohort_prior_decision","receiver_helper_native_timing","receiver_helper_compact_work",
        "receiver_helper_input_view_admission","receiver_helper_compact_phase_attached"};
    constexpr std::size_t maximum_records=4096;
    std::size_t selections=0,wires=0,commits=0,drains=0,omitted=0;
    std::cout<<"# tx_epoch_context columns: profile,helper,control,TEST_actual_TX_epoch,TEST_expected_TX_key_family,diagnostic_primary_capture,diagnostic_primary_id,diagnostic_primary_first_epoch,diagnostic_primary_last_epoch,diagnostic_primary_mixed_epochs,diagnostic_primary_all_epochs_equal_TX,selection_records,joined_final_records,wire_pieces,GUI_commit_records,drain_records,omitted_joined_records,trace\n";
    std::cout<<"# tx_epoch_geometry columns: profile,helper,control,TEST_actual_TX_epoch,input_revision,receiver_key_family,TEST_key_family_match,TX_sample_symbol_DSSS_geometry_match,exact_profile_identity,actual_RF_cell,actual_LF_cell\n";
    std::cout<<"# tx_epoch_wire_piece columns: profile,helper,control,TEST_actual_TX_epoch,input_revision,candidate_id,instance_id,key_family,TEST_key_family_match,sample_origin,first_observed_wire_symbol,observed_piece_bits,unknown_bits,compact_missing_slots,TEST_expected_wire_bits,TEST_overlap_bits,TEST_matching_overlap_bits,piece_complete,collector_scored_END; overlapping repeated pieces are not a unique-bit count; a suffix match never implies a full prefix or completion\n";
    std::cout<<"# fixture_tx_epoch_record JSON wraps actual matching-epoch trace records verbatim. Final numeric/prior/timing records are joined by revision+instance+origin, never general-primary prefix resemblance. Numeric work is per receiver owner, not per cell or candidate. Selection is a pre-constructor policy record, not executed acquisition, and has no instance/profile rank; family association follows ordinary admission's TX-key-first order (after optional plaintext), not new authentication. Exact profile/seed/RF-LF cells and punctuation ranges absent from trace remain unavailable. Drain frequency is an actual detector reference-coordinate estimate, not an RF-cell/clock estimate or a joined instance identity\n";
    const auto emit=[&](const std::string& record) {
        std::cout<<"{\"event\":\"fixture_tx_epoch_record\",\"profile\":"<<audio::trace::quoted(name(profile))
            <<",\"helper\":"<<helper<<",\"control\":"<<audio::trace::quoted(control)
            <<",\"test_TX_epoch\":"<<tx_epoch<<",\"actual_record\":"<<record<<"}\n";
    };
    const auto report_wire=[&](std::uint64_t capture,std::uint64_t candidate,std::uint64_t instance,std::uint64_t key_family,
            std::uint64_t origin,std::uint64_t first,std::span<const std::uint8_t> bits,bool complete,std::optional<bool> end,std::uint64_t missing=0) {
        std::size_t overlap=0,matches=0,unknown=0;
        for(std::size_t i=0;i<bits.size();++i) {
            unknown+=bits[i]>1;
            if(first<expected_wire.size()&&i<expected_wire.size()-first) {
                ++overlap;matches+=bits[i]==expected_wire[static_cast<std::size_t>(first)+i];
            }
        }
        std::cout<<"tx_epoch_wire_piece,"<<name(profile)<<','<<helper<<','<<control<<','<<tx_epoch<<','<<capture<<','
            <<candidate<<','<<instance<<','<<key_family<<','<<(family&&*family==key_family)<<','<<origin<<','<<first<<','
            <<bits.size()<<','<<unknown<<','<<missing<<','<<expected_wire.size()<<','<<overlap<<','<<matches<<','<<complete<<',';
        if(end)std::cout<<*end;else std::cout<<"unavailable";
        std::cout<<'\n';
    };
    for(std::ifstream input(path);input;) {
        std::string line;if(!std::getline(input,line))break;
        const auto epoch=number(line,"epoch");
        const auto event=string_field(line,"event");if(!event||!epoch)continue;
        if(*event=="receiver_signal_commit"&&number(line,"input_revision")==primary.capture_revision&&
           number(line,"signal_id")==primary.id) {
            if(!primary_first_epoch)primary_first_epoch=*epoch;
            else primary_mixed_epochs=primary_mixed_epochs||*primary_first_epoch!=*epoch;
            primary_last_epoch=*epoch;
        }
        if(!tx_epoch||*epoch!=tx_epoch)continue;
        if(*event=="receiver_selection") {
            ++selections;emit(line);
            const bool geometry=number(line,"sample_rate")==transmitted.sample_rate&&
                number(line,"symbol_samples")==modem::symbol_sample_count(transmitted)&&
                number(line,"dsss_factor")==transmitted.dsss_factor&&
                number(line,"fine_chip_samples")==modem::pattern_chip_samples(transmitted);
            std::cout<<"tx_epoch_geometry,"<<name(profile)<<','<<helper<<','<<control<<','<<tx_epoch<<','
                <<number(line,"revision").value_or(0)<<','<<number(line,"family").value_or(UINT64_MAX)<<','
                <<(family&&number(line,"family")==*family)<<','<<geometry<<",unavailable,unavailable,unavailable\n";
        } else if(*event=="receiver_pattern_wire") {
            ++wires;emit(line);
            if(const auto text=string_field(line,"bits")) {
                Bytes bits;bits.reserve(text->size());for(const auto bit:*text)bits.push_back(static_cast<std::uint8_t>(bit-'0'));
                const auto capture=number(line,"input_revision"),candidate=number(line,"candidate_id"),instance=number(line,"instance_id"),
                    key_family=number(line,"family"),origin=number(line,"sample_origin"),first=number(line,"first_stream_symbol");
                if(capture&&candidate&&instance&&key_family&&origin&&first)
                    report_wire(*capture,*candidate,*instance,*key_family,*origin,*first,bits,number(line,"complete").value_or(0),
                        std::nullopt,number(line,"missing_slots").value_or(0));
            }
        } else if(*event=="receiver_signal_commit") {++commits;emit(line);}
        else if(*event=="receiver_pattern_drain") {++drains;emit(line);}
        else for(std::size_t kind=0;kind<joined_events.size();++kind)if(*event==joined_events[kind]) {
            const auto revision=number(line,"revision"),instance=number(line,"instance_id"),origin=number(line,"sample_origin");
            if(!revision||!instance||!origin){++omitted;break;}
            const std::array identity{*revision,*instance,*origin,static_cast<std::uint64_t>(kind)};
            if(final_records.size()==maximum_records&&!final_records.contains(identity)){++omitted;break;}
            final_records[identity]=line;break;
        }
    }
    for(const auto& [identity,line]:final_records){(void)identity;emit(line);}
    if(collector)for(const auto& wire:collector->candidates())if(wire.epoch==tx_epoch)
        report_wire(wire.capture,wire.candidate,wire.instance,wire.family,wire.origin,wire.first_symbol,
            std::span(wire.bits.get(),wire.size),wire.complete,wire.observed_end);
    std::cout<<"tx_epoch_context,"<<name(profile)<<','<<helper<<','<<control<<','<<tx_epoch<<',';
    if(family)std::cout<<*family;else std::cout<<"unavailable";
    std::cout<<','<<primary.capture_revision<<','<<primary.id<<',';
    if(primary_first_epoch)std::cout<<*primary_first_epoch;else std::cout<<"unavailable";
    std::cout<<',';if(primary_last_epoch)std::cout<<*primary_last_epoch;else std::cout<<"unavailable";
    std::cout<<','<<primary_mixed_epochs<<','<<(primary_first_epoch&&!primary_mixed_epochs&&*primary_first_epoch==tx_epoch)
        <<','<<selections<<','<<final_records.size()<<','<<wires<<','
        <<commits<<','<<drains<<','<<omitted<<','<<path.string()<<'\n';
}
struct Result {
    int exit=2;
    Bytes dated_wire;
    std::uint64_t transmit_epoch=0;
    std::uint64_t template_jobs=0,conditional_jobs=0,audit_jobs=0;
    bool work_traced=false;
    CompactWork compact{};
    NativeArithmetic native_arithmetic{};
    bool compact_work_traced=false;
    std::uint64_t input_view_jobs=0,unique_conditional_jobs=0;
    bool primary_input_view_certified=false,later_input_view_assisted=false;
    std::uint64_t captured_samples=0;
    std::size_t workspace_bytes=0;
    double cpu_seconds=0,wall_seconds=0,maximum_backlog_seconds=0;
};
struct PublicHelperObservation {
    bool ever_measured=false,ever_frequency_active=false,ever_phase_active=false,ever_timing_active=false;
    std::optional<double> last_snr,last_frequency_offset;
    std::uint64_t last_measured_at=0,maximum_initial_fft=0;
    std::array<std::uint64_t,compact_counter_count> maximum_compact_samples{};
    double maximum_backlog_seconds=0;
    std::uint64_t maximum_input_view_jobs=0;
    std::size_t maximum_input_view_count=0;
    std::uint32_t maximum_hardware_sample_rate=0;
    double minimum_reported_audio_passband_hz=0,maximum_reported_audio_passband_hz=0;
    void observe(const live::Snapshot& snapshot) {
        ever_frequency_active=ever_frequency_active||snapshot.helper_frequency_assistance_active;
        ever_phase_active=ever_phase_active||snapshot.helper_phase_assistance_active;
        ever_timing_active=ever_timing_active||snapshot.helper_timing_assistance_active;
        maximum_initial_fft=std::max(maximum_initial_fft,snapshot.receiver_initial_fft_input_samples);
        maximum_backlog_seconds=std::max(maximum_backlog_seconds,snapshot.receiver_backlog_seconds);
        maximum_input_view_jobs=std::max(maximum_input_view_jobs,snapshot.helper_input_view_conditional_jobs);
        maximum_input_view_count=std::max(maximum_input_view_count,snapshot.helper_input_view_count);
        maximum_hardware_sample_rate=std::max(maximum_hardware_sample_rate,snapshot.hardware_sample_rate);
        if(std::isfinite(snapshot.audio_passband_hz)&&snapshot.audio_passband_hz>0) {
            minimum_reported_audio_passband_hz=minimum_reported_audio_passband_hz?
                std::min(minimum_reported_audio_passband_hz,snapshot.audio_passband_hz):snapshot.audio_passband_hz;
            maximum_reported_audio_passband_hz=std::max(maximum_reported_audio_passband_hz,snapshot.audio_passband_hz);
        }
        const std::array<std::uint64_t,compact_counter_count> compact{
            snapshot.helper_compact_projected_samples,snapshot.helper_compact_lane_samples,
            snapshot.helper_compact_skipped_projected_samples,snapshot.helper_compact_skipped_lane_samples,
            snapshot.helper_compact_audit_projected_samples,snapshot.helper_compact_audit_lane_samples};
        for(std::size_t i=0;i<compact.size();++i)
            maximum_compact_samples[i]=std::max(maximum_compact_samples[i],compact[i]);
        if(snapshot.helper_snr_db&&snapshot.helper_frequency_offset_hz) {
            ever_measured=true;last_snr=snapshot.helper_snr_db;last_frequency_offset=snapshot.helper_frequency_offset_hz;
            // Poll frontier only; Snapshot does not expose the actual raw
            // reference measurement date or its age.
            last_measured_at=snapshot.samples_received;
        }
    }
};
bool requires_v9_assistance(Profile profile) {
    return profile==Profile::hf_crystal||profile==Profile::hf_fhss||profile==Profile::eme;
}
bool assistance_trace_qualified(Profile profile,bool helper,const TraceResult& observed,
                                std::uint64_t native_conditional_jobs,bool detailed) {
    if(!helper)return true;
    if(observed.realtime_trace_present)return observed.realtime_primary_qualified&&observed.realtime_later_qualified;
    if(profile==Profile::long_auto)return observed.cohort_frontend&&observed.cohort_resources_valid&&
        native_conditional_jobs>0&&observed.assisted&&(detailed||observed.wire_assisted);
    if(!requires_v9_assistance(profile))return true;
    if(profile==Profile::hf_crystal) {
        // Wide correction constructs its own conditional RF/clock catalogue.
        // Its original-mask prior correctly remains inactive. Require the
        // primary ordinary wire's committed, causal, clock-bounded birth and
        // actual native execution rather than a frontend-enable indication.
        return observed.cohort_resources_valid&&observed.primary_input_view_certified&&observed.input_view_jobs>0&&
            observed.frequency_narrowed&&observed.later_input_view_assisted&&observed.assisted&&
            (detailed||observed.wire_assisted);
    }
    // Executed omissions are distinct from prospective pair-count estimates.
    // EME specifically requires the ordinary compact engine to do useful work
    // while omitting cold acquisition lanes under an applied measured prior.
    const bool executed=profile==Profile::eme?observed.compact.executed_and_omitted():
        native_conditional_jobs>0||observed.compact.executed_and_omitted();
    return observed.cohort_frontend&&observed.cohort_resources_valid&&executed&&observed.frequency_narrowed&&
        !observed.narrowed_instances.empty()&&observed.later_assisted_epoch&&observed.assisted&&
        (detailed||observed.wire_assisted);
}
Message source_message(const Options& options) {
    Message message;
    if(options.payload==Payload::text)message.data=Bytes{'t','a','i','l',' ','c','o','m','p','l','e','t','e',' ','t','e','s','t'};
    if(options.payload==Payload::attachment) {
        message.kind=MessageKind::file;message.filename="normal-tail.bin";
        for(unsigned i=0;i<17;++i)message.data.push_back(static_cast<std::uint8_t>(i*37+11));
    }
    if(!options.source_bytes)return message;
    message.data.resize(*options.source_bytes);
    std::uint32_t state=0x9e3779b9U;
    const auto next=[&]() {state^=state<<13;state^=state>>17;state^=state<<5;return state;};
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?-:;";
    for(auto& byte:message.data)byte=options.payload==Payload::text?
        static_cast<std::uint8_t>(alphabet[next()%alphabet.size()]):static_cast<std::uint8_t>(next()>>16);
    const Bytes tail=options.payload==Payload::text?Bytes{'_','E','N','D','_','t','e','x','t','_','9','Z'}:
        Bytes{0xD3,0x06,0x00,0xA5,0x71,0xFE,0x29,0x8C,0x42,0x19,0xE7,0x5B};
    const auto count=std::min(tail.size(),message.data.size());
    std::copy(tail.end()-static_cast<std::ptrdiff_t>(count),tail.end(),message.data.end()-static_cast<std::ptrdiff_t>(count));
    return message;
}
Result run(Profile profile,bool helper,const Resolved& resolved,const Options& options) {
    const auto& value=resolved.settings;const auto& c=value.transfer.modem;
    const auto message=source_message(options);
    if(options.payload!=Payload::bits)std::cout<<"source,"<<name(profile)<<','<<helper<<','<<name(options.payload)<<','
        <<message.data.size()<<','<<message.filename<<'\n';
    const auto path=options.evidence/(std::string(name(profile))+"-"+(helper?"on":"off")+"-"+name(options.payload)+"-"+
        (resolved.gpsdo_reference?"gpsdo-reference-":"")+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::filesystem::create_directories(path.parent_path());
    std::atomic<std::int64_t> replay_ms{0};Bytes expected,dated_wire;
    live::Snapshot last;bool finished=false,exact=false,runtime_error=false,observation_reached=false;
    PublicHelperObservation helper_observation;
    std::uint64_t transmit_epoch=0,max_workspace=0;
    std::uint64_t maximum_snapshot_conditional_jobs=0;
    const bool detailed=options.payload==Payload::bits;
    const bool collect_work=detailed||options.work_trace||requires_v9_assistance(profile);
    WireCollector collector(c.sample_rate);
    trace_detail.store(detailed,std::memory_order_relaxed);
    trace_work.store(collect_work,std::memory_order_relaxed);
    trace_tx_diagnostics.store(options.runtime_metrics,std::memory_order_relaxed);
    const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
    auto metrics=options.runtime_metrics?std::make_unique<RuntimeMetrics>(c.sample_rate):nullptr;
    {
        // A 128-second EME symbol needs a long diagnostic, while lane-work
        // counters can change every feed. Sample only cumulative work at a
        // declared 16-second cadence, identically for Helper ON and OFF. Source,
        // masks, dispatch, qualification and lifecycle changes remain exact.
        // The filter separately retains dated pure-pilot phase/reference events;
        // this permits sampling the bank's last-view phase flag, not phase work.
        const auto work_interval=profile==Profile::eme&&collect_work?16ULL*c.sample_rate:0;
        audio::trace::Scope trace(path,false,&trace_filter,work_interval,work_interval!=0);
        Dates dates(detailed?nullptr:&collector,metrics.get());
        // Empty EpochClock retains the normal sample-driven simulation path.
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);
        // Match Controller::transmit_config(): raw binary and short text use
        // the short profile; framed text/files use the configured long one.
        // Session's empty hopping argument deliberately disables TX hopping.
        const bool short_payload=options.payload==Payload::bits||transfer::uses_raw_message(message);
        const auto& transmit_config=!short_payload&&value.long_message_modem?*value.long_message_modem:c;
        if(options.payload==Payload::bits)session.transmit_bits(known_bits(options.bit_count),false,transmit_config.fake_hopping);
        else session.transmit(message,false,transmit_config.fake_hopping);
        const auto deadline=start+std::chrono::seconds(options.wall_seconds);
        while(std::chrono::steady_clock::now()<deadline) {
            last=session.snapshot();max_workspace=std::max<std::uint64_t>(max_workspace,last.dsp_buffered_bytes);
            if(metrics)metrics->poll(last);
            helper_observation.observe(last);
            maximum_snapshot_conditional_jobs=std::max(maximum_snapshot_conditional_jobs,last.helper_conditional_jobs);
            if(!last.error.empty()||last.receiver_health.receiver_reset||last.receiver_health.input_interrupted||
                last.receiver_health.input_overruns||last.receiver_health.dropped_samples) {runtime_error=true;break;}
            if(last.transmission_finished&&last.simulation_replay) {
                replay_ms=4000;finished=true;last=session.snapshot();
                if(metrics)metrics->poll(last);
                helper_observation.observe(last);
                maximum_snapshot_conditional_jobs=std::max(maximum_snapshot_conditional_jobs,last.helper_conditional_jobs);break;
            }
            // A manual captured-media frontier ends only this diagnostic.
            // Ordinary completion above and every receive assertion below
            // remain authoritative; a reached window alone is incomplete.
            if(options.observe_samples&&last.samples_received>=*options.observe_samples) {
                observation_reached=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        observation_reached=options.observe_samples&&last.samples_received>=*options.observe_samples;
        transmit_epoch=dates.transmit.load();
        if(metrics)metrics->before_stop=ProcessStamp::now();
        session.stop();
    }
    if(metrics)metrics->joined=ProcessStamp::now();
    if(transmit_epoch) {
        auto dated=value.transfer;dated.timestamp=transmit_epoch;
        if(options.payload!=Payload::bits&&value.long_message_modem&&!transfer::uses_raw_message(message))
            dated.modem=*value.long_message_modem;
        if(options.payload==Payload::bits) {expected=known_bits(options.bit_count);dated_wire=expected;transfer::xor_binary_bits(dated_wire,dated);}
        else {dated_wire=transfer::message_wire_bits(message,dated);expected=dated_wire;transfer::xor_binary_bits(expected,dated);}
        if(options.payload==Payload::bits)for(const auto& signal:last.signals) {
            if(signal.helper_carrier)continue;const auto raw=signal.binary?signal.text:signal.raw_bits;
            exact=exact||(signal.complete&&signal.received_bits==expected.size()&&raw==binary(expected));
        } else for(const auto& received:last.received) {
            const auto preview=std::min<std::size_t>(4096,expected.size());
            const bool decoded=transfer::uses_raw_message(message)?received.short_text_decoded:
                received.content_validated&&received.content.authenticated;
            exact=exact||(received.stream_complete&&decoded&&
                received.observed_bits==expected.size()&&received.raw_bits.size()==preview&&
                std::equal(received.raw_bits.begin(),received.raw_bits.end(),expected.begin())&&
                received.content.message.kind==message.kind&&received.content.message.filename==message.filename&&
                received.content.message.data==message.data);
        }
    }
    if(!detailed)collector.report(path);
    const auto ordinary_baseline=modem::oscillator_pattern_search(c,
        transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(c):0);
    const auto observed=inspect_trace(path,expected,dated_wire,c,detailed?nullptr:&collector,&ordinary_baseline);
    if(metrics)report_tx_epoch_diagnostic(profile,helper,resolved,
        options.payload!=Payload::bits&&value.long_message_modem&&!transfer::uses_raw_message(message)?*value.long_message_modem:c,
        transmit_epoch,dated_wire,observed,path,detailed?nullptr:&collector);
    const auto latency=metrics?decision_timing(*metrics,path,observed):DecisionTiming{};
    const auto assistance_jobs=collect_work?observed.unique_conditional_jobs:maximum_snapshot_conditional_jobs;
    std::cout<<"assistance_evidence,"<<name(profile)<<','<<helper<<','
        <<(detailed?"detailed_bits":collect_work?"light_framed_with_work":"light_framed")<<','
        <<(collect_work?"traced_per_instance_unique_native_conditional":"maximum_public_snapshot")<<','<<assistance_jobs<<','
        <<maximum_snapshot_conditional_jobs<<','<<observed.trace_complete<<','<<observed.wire_evidence_complete<<','
        <<observed.wire_assisted<<'\n';
    // These normal-use acceptance scenarios require actual assisted-lane
    // evidence. Metadata cannot prove the private control allocation or
    // received-reference admission, and an ordinary-only decode is insufficient.
    const bool assistance_required=helper&&(profile==Profile::long_auto||requires_v9_assistance(profile));
    const bool assistance_qualified=assistance_trace_qualified(profile,helper,observed,assistance_jobs,detailed);
    const bool success=finished&&exact&&observed.pending&&observed.complete&&observed.observed_end&&
        observed.wire_exact&&observed.wire_contiguous&&observed.wire_complete&&
        observed.identity&&observed.prefix&&observed.all_rows_end&&observed.later_epoch&&!runtime_error&&assistance_qualified&&
        observed.trace_complete&&observed.wire_evidence_complete;
    // A time limit with partial input/decisions is explicitly incomplete.
    // Incorrect completed payload, physical END or progress is a failure.
    const bool invalid=runtime_error||!observed.identity||!observed.prefix||!observed.all_rows_end||
        (assistance_required&&!observed.cohort_resources_valid)||
        (observed.wire_bits&&!observed.wire_contiguous)||
        (finished&&(!exact||!observed.complete||!observed.observed_end||!observed.wire_exact||!observed.wire_complete||!assistance_qualified));
    const bool incomplete_evidence=!observed.trace_complete||!observed.wire_evidence_complete;
    const int exit=runtime_error?1:incomplete_evidence?2:success?0:invalid?1:2;
    if(metrics)metrics->posthoc=ProcessStamp::now();
    const auto process_cpu_seconds=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;
    const auto process_wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<std::setprecision(17)<<"result,"<<name(profile)<<','<<helper<<','<<name(options.payload)<<','
             <<(runtime_error?"failed_runtime":incomplete_evidence?"incomplete_evidence":success?"decoded_exact":invalid?"failed":
                options.observe_samples?(observation_reached?"incomplete_observation_window":"incomplete_observation_deadline"):"incomplete")<<','<<transmit_epoch<<','<<expected.size()<<','
             <<observed.bits<<','<<observed.wire_bits<<','<<observed.wire_exact<<','<<observed.wire_contiguous<<','<<observed.wire_complete<<','
             <<exact<<','<<observed.pending<<','<<observed.complete<<','<<observed.observed_end<<','
             <<observed.identity<<','<<observed.prefix<<','<<observed.rows<<','<<observed.secondary_rows<<','<<observed.all_rows_end<<','
             <<observed.later_epoch<<','<<observed.assisted<<','<<observed.phase<<','
             <<last.samples_received<<','<<last.helper_original_search_pairs<<','<<last.helper_selected_search_pairs<<','
             <<last.helper_conditional_jobs<<','<<last.helper_audit_jobs<<','<<observed.work_instances<<','
             <<observed.template_jobs<<','<<observed.input_transforms<<','<<observed.conditional_jobs<<','
             <<observed.audit_jobs<<','<<last.receiver_health.input_overruns<<','<<last.receiver_health.dropped_samples<<','
             <<last.receiver_health.input_interrupted<<','<<last.receiver_health.receiver_reset<<','
             <<last.receiver_health.search_limited<<','<<max_workspace<<','
             <<process_cpu_seconds<<','<<process_wall_seconds<<','<<path.string()<<'\n';
    std::cout<<"native_arithmetic,"<<name(profile)<<','<<helper<<','<<observed.native_arithmetic.complete<<','
        <<observed.native_arithmetic.instances<<','<<observed.work_instances;
    print_native_arithmetic(observed.native_arithmetic);
    std::cout<<','<<observed.template_jobs<<','<<observed.input_transforms<<','<<process_cpu_seconds<<','
        <<process_wall_seconds<<','<<helper_observation.maximum_backlog_seconds<<','<<last.samples_received<<','
        <<value.dsp_workspace_bytes<<','<<path.string()<<'\n';
    std::cout<<"compact_work,"<<name(profile)<<','<<helper<<','<<observed.compact.instances;
    for(const auto counter:observed.compact.samples)std::cout<<','<<counter;
    std::cout<<','<<collect_work<<','<<observed.trace_complete<<','<<observed.compact.executed_and_omitted()<<','
        <<observed.frequency_narrowed<<','<<observed.narrowed_instances.size()<<','<<observed.later_assisted_epoch<<','
        <<observed.timing<<','<<path.string()<<','<<observed.compact.capability_supported_instances<<'\n';
    std::cout<<"input_view_work,"<<name(profile)<<','<<helper<<','<<observed.input_view_admissions<<','
        <<observed.input_view_instances<<','<<observed.input_view_jobs<<','<<observed.conditional_jobs<<','
        <<observed.unique_conditional_jobs<<','<<observed.template_jobs<<','<<observed.primary_input_view_certified<<','
        <<observed.later_input_view_assisted<<','<<helper_observation.maximum_input_view_jobs<<','
        <<helper_observation.maximum_input_view_count<<','<<assistance_qualified<<','<<path.string()<<'\n';
    std::cout<<"helper_applied_state,"<<name(profile)<<','<<helper<<','
        <<last.helper_frequency_assistance_active<<','<<last.helper_phase_assistance_active<<','<<last.helper_timing_assistance_active<<','
        <<helper_observation.ever_frequency_active<<','<<helper_observation.ever_phase_active<<','<<helper_observation.ever_timing_active;
    for(const auto counter:helper_observation.maximum_compact_samples)std::cout<<','<<counter;
    std::cout<<','<<helper_observation.maximum_backlog_seconds<<','<<last.receiver_backlog_seconds<<','
        <<helper_observation.maximum_hardware_sample_rate<<','<<helper_observation.minimum_reported_audio_passband_hz<<','
        <<helper_observation.maximum_reported_audio_passband_hz<<','<<helper_observation.maximum_initial_fft<<','
        <<assistance_qualified<<','
        <<std::quoted(last.helper_status)<<','<<std::quoted(last.receiver_error)<<','<<path.string()<<'\n';
    if(metrics) {
        metrics->report(profile,helper,resolved.gpsdo_reference,path);
        std::cout<<"decision_timing,"<<name(profile)<<','<<helper<<','
            <<(resolved.gpsdo_reference?"gpsdo-simulation-reference":"declared")<<','<<observed.capture_revision<<','
            <<observed.id<<','<<latency.decisions<<','<<latency.availability_joins<<','<<latency.publication_joins<<','
            <<latency.replay_publications<<',';
        if(latency.decisions)std::cout<<latency.maximum_lookahead_seconds;else std::cout<<"unavailable";
        std::cout<<',';
        if(latency.decisions&&latency.availability_joins==latency.decisions&&!metrics->invalid&&!metrics->feed_overflow)
            std::cout<<latency.maximum_availability_wall_seconds;
        else std::cout<<"unavailable";
        std::cout<<',';
        if(latency.decisions&&latency.publication_joins==latency.decisions&&!metrics->poll_overflow)
            std::cout<<latency.maximum_publication_wall_seconds;
        else std::cout<<"unavailable";
        std::cout<<','<<path.string()<<'\n';
    }
    if(options.observe_samples) {
        std::string backends;
        for(const auto& backend:observed.selection_backends) {if(!backends.empty())backends+=';';backends+=backend;}
        const auto print_optional=[](const std::optional<double>& number) {
            if(number)std::cout<<*number;else std::cout<<"unavailable";
        };
        std::cout<<"observation_window,"<<name(profile)<<','<<helper<<','<<*options.observe_samples<<','
            <<observation_reached<<','<<finished<<','<<exit<<','
            <<(success?"ordinary_full_completion":exit==1?"FAILED":incomplete_evidence?"INCOMPLETE_EVIDENCE":
               observation_reached?"INCOMPLETE_OBSERVATION_WINDOW":"INCOMPLETE_OBSERVATION_DEADLINE")<<','
            <<last.samples_received<<','<<static_cast<double>(last.samples_received)/c.sample_rate<<','
            <<last.receiver_initial_fft_input_samples<<','<<helper_observation.maximum_initial_fft<<','
            <<last.clock_window_modeled<<','<<std::quoted(backends.empty()?"unavailable":backends)<<",unavailable,"
            <<static_cast<bool>(last.helper_snr_db&&last.helper_frequency_offset_hz)<<','<<helper_observation.ever_measured<<','
            <<helper_observation.last_measured_at<<',';
        print_optional(helper_observation.last_snr);std::cout<<',';print_optional(helper_observation.last_frequency_offset);
        // Snapshot exposes Tracker's 10Hz-cell SNR/offset and applied flags.
        // It exposes no precision Receiver reference, C/N0 or timing validity.
        std::cout<<",unavailable,unavailable,unavailable,unavailable,"
            <<last.helper_frequency_assistance_active<<','<<last.helper_phase_assistance_active<<','
            <<helper_observation.ever_frequency_active<<','<<helper_observation.ever_phase_active<<','
            <<last.helper_original_search_pairs<<','<<last.helper_selected_search_pairs<<','
            <<last.helper_conditional_jobs<<','<<maximum_snapshot_conditional_jobs<<','<<last.helper_audit_jobs<<','
            <<observed.template_jobs<<','<<observed.conditional_jobs<<','<<observed.audit_jobs<<','
            <<last.receiver_instances<<','<<last.receiver_admissions<<','<<last.receiver_retirements<<','
            <<last.receiver_health.input_overruns<<','<<last.receiver_health.dropped_samples<<','
            <<last.receiver_health.input_interrupted<<','<<last.receiver_health.receiver_reset<<','
            <<last.receiver_health.search_limited<<','<<std::quoted(last.helper_status)<<','
            <<std::quoted(last.receiver_error)<<','<<std::quoted(last.status)<<','<<std::quoted(last.error)<<','<<path.string()<<'\n';
    }
    if(!last.error.empty())std::cerr<<name(profile)<<" helper="<<helper<<" runtime error: "<<last.error<<'\n';
    if(!last.receiver_error.empty())std::cerr<<name(profile)<<" helper="<<helper<<" receiver eligibility: "<<last.receiver_error<<'\n';
    Result result{exit,std::move(dated_wire),transmit_epoch,observed.template_jobs,
        observed.conditional_jobs,observed.audit_jobs,collect_work&&observed.trace_complete&&observed.work_instances!=0&&!runtime_error};
    result.compact=observed.compact;
    result.native_arithmetic=observed.native_arithmetic;
    result.compact_work_traced=collect_work&&observed.trace_complete&&observed.compact.instances&&!runtime_error;
    result.captured_samples=last.samples_received;
    result.workspace_bytes=value.dsp_workspace_bytes;
    result.input_view_jobs=observed.input_view_jobs;result.unique_conditional_jobs=observed.unique_conditional_jobs;
    result.primary_input_view_certified=observed.primary_input_view_certified;
    result.later_input_view_assisted=observed.later_input_view_assisted;
    result.cpu_seconds=process_cpu_seconds;result.wall_seconds=process_wall_seconds;
    result.maximum_backlog_seconds=helper_observation.maximum_backlog_seconds;
    return result;
}
int reinspect(const Options& options,const Resolved& resolved) {
    // Analysis of a pre-existing, sealed binary-wire trace only. The supplied
    // actual TX date is used after capture to reconstruct its expected mask.
    // No Session is started; no receiver, PCM, source decoder or capture is created.
    const auto profile=options.profiles.front();const auto helper=options.helpers.front();
    const auto expected=known_bits(options.bit_count);auto dated=resolved.settings.transfer;
    dated.timestamp=*options.inspect_epoch;auto wire=expected;transfer::xor_binary_bits(wire,dated);
    const auto observed=inspect_trace(*options.inspect_path,expected,wire,dated.modem);
    if(options.runtime_metrics)report_tx_epoch_diagnostic(options.profiles.front(),options.helpers.front(),resolved,
        dated.modem,*options.inspect_epoch,wire,observed,*options.inspect_path);
    const bool assisted=assistance_trace_qualified(profile,helper,observed,observed.unique_conditional_jobs,true);
    const bool valid=observed.pending&&observed.complete&&observed.bits==expected.size()&&observed.observed_end&&
        observed.wire_exact&&observed.wire_contiguous&&observed.wire_complete&&observed.identity&&observed.prefix&&
        observed.all_rows_end&&observed.later_epoch&&assisted;
    const int status=!observed.trace_complete||!observed.wire_evidence_complete?2:valid?0:1;
    std::cout<<"posthoc_reinspection,"<<name(profile)<<','<<helper<<','<<*options.inspect_epoch<<','<<options.bit_count<<','
        <<(status==2?"incomplete_evidence":valid?"trace_checks_passed":"trace_checks_failed")<<','
        <<observed.id<<','<<observed.revision<<','<<observed.bits<<','<<observed.wire_bits<<','
        <<observed.pending<<','<<observed.complete<<','<<observed.observed_end<<','<<observed.wire_exact<<','
        <<observed.wire_contiguous<<','<<observed.wire_complete<<','<<observed.identity<<','<<observed.prefix<<','
        <<observed.all_rows_end<<','<<observed.rows<<','<<observed.assisted<<','<<options.inspect_path->string()<<'\n';
    std::cout<<"posthoc_compact_work,"<<name(profile)<<','<<helper<<','<<observed.compact.instances;
    for(const auto counter:observed.compact.samples)std::cout<<','<<counter;
    std::cout<<','<<observed.compact.capability_supported_instances<<','<<observed.trace_complete<<','
        <<options.inspect_path->string()<<'\n';
    std::cout<<"# posthoc_reinspection checks the original sealed trace only; it does not rerun reception, Snapshot source admission or runtime health, and never replaces the original run outcome\n";
    return status;
}
}
int main(int argc,char** argv) {try {
    auto options=parse(argc,argv);row_progress_guards();EphemeralKey key(options);int status=0;
    if(options.inspect_path) {
        std::cout<<"# posthoc_reinspection columns: profile,helper,actual_TX_epoch,actual_bit_count,trace_result,primary_id,primary_revision,primary_bits,primary_wire_bits,pending,complete,scored_END,wire_equal,wire_contiguous,wire_complete,revision_identity,primary_lifetime_prefix,all_historical_completed_END,rows,primary_assisted,original_trace\n";
        std::cout<<"# posthoc_compact_work columns: profile,helper,traced_compact_instances,projected_carrier_samples,accumulated_lane_samples,skipped_carrier_samples,skipped_lane_samples,broad_audit_projected_carrier_samples,broad_audit_accumulated_lane_samples,capability_supported_instances,complete_trace,original_trace; executed counter maxima do not qualify capability or replace the original run result\n";
        return reinspect(options,resolve(options.profiles.front(),options.helpers.front(),options));
    }
    std::cout<<"# metadata columns: profile,helper,Fs,bandwidth,SF,DSSS,symbol_samples,symbol_seconds,pairs,frequencies,rates,requested_Hz,covered_Hz,limited,dsp_bytes,bank_bytes,simulation_snr,CFO,clock_ppm,phase_noise,RX_keys\n";
    std::cout<<"# result columns: profile,helper,payload,result,TX_epoch,expected_bits,primary_bits,primary_wire_bits,full_physical_wire_exact,primary_wire_contiguous,primary_wire_complete,exact_source_and_diagnostic_prefix,primary_pending,primary_complete,primary_scored_END,per_row_identity,per_row_prefix,rows,secondary_rows,all_completed_rows_scored_END,later_epoch,primary_assisted,phase,samples,original_pairs,selected_pairs,reported_conditional_jobs,reported_audit_jobs,traced_instances,observed_template_jobs,observed_input_transforms,observed_conditional_jobs,observed_audit_jobs,input_overruns,dropped_samples,input_interrupted,receiver_reset,search_limited,peak_dsp_bytes,process_CPU_seconds,wall_seconds,trace\n";
    std::cout<<"# trace_row columns: trace,capture_revision,signal_id,row_revision,physical_first,bits,pending,complete,scored_END,monotone_revision_and_within_revision_count,within_revision_prefix,expected_start_prefix_across_revisions,primary,assisted; primary additionally retains strict count/prefix/completion lifetime across revisions\n";
    std::cout<<"# trace_wire columns: trace,capture_revision,candidate_id,instance_id,stream_first_sample,stream_first_symbol,bits,contiguous,consistent,complete,scored_END,entire_dated_wire_equal,primary\n";
    std::cout<<"# source columns: profile,helper,payload,actual_source_bytes,filename; raw_bits diagnostics retain min(actual_wire_bits,4096), while the wire trace or bounded observer verifies the entire physical wire\n";
    std::cout<<"# auto_geometry columns: profile,helper,profile_index,automatic_profile_count,unique_keys,actual_backend,original_pairs,full_frequency_coverage,full_clock_coverage,wanted_low_Hz,wanted_high_Hz,helper_Hz,certified_FIR_amplitude_error,filter_delay\n";
    std::cout<<"# auto_geometry_gate columns: profile,helper,profile_index,supported,actual_backend,full_frequency_coverage,full_clock_coverage,oscillator_limited,wanted_low_Hz,wanted_high_Hz,helper_Hz,certified_FIR_amplitude_error,selection_reason; emitted before the unchanged eligibility assertion\n";
    std::cout<<"# auto_resource_geometry columns: profile,helper,Fs,actual_filter_bytes,public_cohort_reserve_bytes,remaining_bytes_for_private_control; full control admission requires the physical trace\n";
    std::cout<<"# helper_resource_trace columns: trace,frontend_admitted,phase_storage_observed,maximum_actual_bytes,minimum_reported_reserve_bytes,within_public_reserve; long-auto helper ON also requires conditional work and primary assisted provenance\n";
    std::cout<<"# trace_integrity columns: trace,complete_footer,matching_begin_end_tokens,dropped,size_limited,prior_io_failed,complete_evidence; incomplete evidence returns status 2 without payload qualification, while actual runtime or health failures retain status 1\n";
    std::cout<<"# assistance_evidence columns: profile,helper,trace_mode,conditional_count_source,conditional_jobs_for_assistance_gate,maximum_public_snapshot_conditional_jobs,complete_trace,complete_wire_evidence,primary_wire_helper_reference_acquired; the selected primary wire requires actual conditioned birth or the legacy helper tag, never enabled helper/global jobs. Framed payloads retain the bounded typed wire observer. Without --work-trace or a new acceptance profile, light framed mode uses Snapshot maxima and establishes no total-work gain\n";
    std::cout<<"# compact_work columns: profile,helper,traced_compact_instances,projected_carrier_samples,accumulated_lane_samples,skipped_carrier_samples,skipped_lane_samples,broad_audit_projected_carrier_samples,broad_audit_accumulated_lane_samples,work_trace_requested,complete_trace,executed_and_omitted_cold_lane_work,qualified_frequency_narrowing,narrowed_instances,later_assisted_epoch,timing_narrowing_applied,trace,capability_supported_instances; actual cumulative maxima per joined ordinary receiver, never FFT jobs or capability/qualification inferred from executed work\n";
    std::cout<<"# native_arithmetic columns: profile,helper,complete_counter_coverage,traced_counter_instances,ordinary_work_instances,fft_jobs,direct_jobs,exact_reused_template_jobs,partitioned_transform_points,native_fp32_transforms,tabulated_fp64_transforms,template_jobs,input_transforms,process_CPU_seconds,wall_seconds,maximum_backlog_seconds,captured_samples,matched_DSP_workspace_bytes,trace; separate cumulative maxima per joined ordinary receiver through retirement/unwind, no sum across unlike arithmetic units\n";
    std::cout<<"# template_jobs and input_view_native_jobs include exact-reused template descriptor visits; they are not FFT executions. Arithmetic absent from a historical or incomplete counter schema is unavailable, never inferred as zero. Compact sample work remains separate\n";
    std::cout<<"# input_view_admission columns: trace,revision,instance_id,sample_origin,epoch,family,view_id,baseline_original_pairs,actual_conditional_pairs,raw_frequency_Hz,raw_frequency_uncertainty_Hz,raw_reference_sample,raw_frequency_sample,physical_sample_rate,captured_ingress,helper_source_origin,immutable_correction_Hz,conjugate,qualified_clock_lower_ppm,qualified_clock_upper_ppm,later_epoch,joined_executed_native_jobs,primary_wire_view; committed admitted+qualified records only, causal <=1s frequency age and clock interval within the declared prior, not full baseline RF coverage\n";
    std::cout<<"# input_view_work columns: profile,helper,committed_admission_records,executed_view_instances,input_view_native_jobs,raw_core_conditional_jobs,unique_conditional_native_jobs,total_native_jobs,primary_wire_view_certified,later_view_assisted_epoch,maximum_snapshot_view_jobs,maximum_snapshot_view_count,assistance_qualified,trace; view jobs are a subset of total_native_jobs. Unique conditional counts each view job once, excluding overlap with its raw core conditional/audit work. No compact sample work is converted to native jobs\n";
    std::cout<<"# compact projection counts exclude shared-cache owner generation and cache hits; lane counts are actual per-fit sample consumption. Skipped counters report conditional omissions, not executed arithmetic. CPU includes the whole process. Neither counters nor pair estimates alone establish end-to-end gain\n";
    std::cout<<"# helper_applied_state columns: profile,helper,current_frequency,current_phase,current_timing,ever_frequency,ever_phase,ever_timing,maximum_snapshot_projected_samples,maximum_snapshot_lane_samples,maximum_snapshot_skipped_projected_samples,maximum_snapshot_skipped_lane_samples,maximum_snapshot_audit_projected_samples,maximum_snapshot_audit_lane_samples,maximum_backlog_seconds,final_backlog_seconds,maximum_reported_hardware_rate,minimum_reported_audio_passband_Hz,maximum_reported_audio_passband_Hz,maximum_initial_FFT_samples,assistance_qualified,helper_reason,receiver_eligibility,trace; snapshot maxima are not per-instance work sums, zero hardware/passband means unavailable\n";
    std::cout<<"# wire_collector columns: trace,complete_evidence,failure,observations,candidates,logical_bit_bytes,current_data_capacity_bytes,peak_data_capacity_bytes_including_transient_growth,data_capacity_bound_bytes,fixed_candidate_metadata_bytes,collector_control_bytes,candidate_capacity; diagnostic element capacities only, never a DSP budget claim\n";
    std::cout<<"# observed work sums each traced instance's maximum cumulative execution counters; no work is inferred from search-pair counts or absent trace records\n";
    std::cout<<"# prior_geometry columns: profile,helper,original_pairs,predicted_selected_pairs,original_clock_lanes,represented_clock_lanes,assumed_reference_uncertainty_Hz,predicted_helper_offset_Hz,predicted_mask_radius_Hz,authoritative_clock_cell_half_width_ppm,predicted_prior_admitted; shared-selector geometry only, not a received measurement\n";
    std::cout<<"# realtime_work_provenance columns: trace,revision,instance_id,sample_origin,epoch,family,conditional_original_pairs,selected_pairs,clock_qualified,unresolved_clock_cell_half_width_ppm,separate_ordinary_baseline_pairs,physical_geometry_matched,qualified_executed_work,conditional_birth_smaller_than_baseline,actual_in_owner_omissions,qualified,selected_primary_owner; original factory baseline is test-only metadata; counts imply no CPU gain and expected length never enters RX\n";
    std::cout<<"# workspace columns: profile,helper,requested_percent,Controller_resolved_bytes,matched_workspace_bytes,matched_bank_bytes,policy\n";
    if(options.runtime_metrics) {
        std::cout<<"# runtime_cpu columns: profile,helper,control,complete_feed_intervals,invalid_checkpoint_joins,feed_overflow,poll_overflow,unfinished_reads,uncommitted_final_read,startup_process_CPU_seconds,startup_wall_seconds,active_after_first_read_process_CPU_seconds,active_after_first_read_wall_seconds,stop_join_trace_process_CPU_seconds,stop_join_trace_wall_seconds,posthoc_process_CPU_seconds,posthoc_wall_seconds,payload_wall_media_factor,simulation_queue_backlog_seconds,trace\n";
        std::cout<<"# runtime_commit_accounting columns: profile,helper,control,intermediate_advancing_commits,empty_PCM_commits,standalone_commits,standalone_same_frontier_commits,standalone_other_frontier_commits,active_source_interval_order_valid,active_generation_process_CPU_seconds,active_feed_process_CPU_seconds,active_process_CPU_outside_source_intervals,total_process_CPU_seconds,phase_sum_process_CPU_seconds,phase_sum_residual_CPU_seconds,standalone_numeric_CPU_attribution,trace; intermediate/empty callbacks never close a source packet or add audio, final exact packet join remains mandatory. Standalone work has no before_feed start stamp and its CPU attribution is unavailable; all process CPU remains in the phase reconciliation. Private observer revision only\n";
        std::cout<<"# runtime_feed columns: profile,helper,control,index,input_revision,source_begin,source_end,bank_begin,bank_end,sample_rate,read_available_steady,feed_available_steady,commit_steady,generation_process_CPU_seconds,generation_wall_seconds,ordinary_feed_process_CPU_seconds,ordinary_feed_wall_seconds,trace\n";
        std::cout<<"# decision_timing columns: profile,helper,control,input_revision,primary_signal_id,new_bit_progress_commits,block_availability_joins,public_poll_joins,presentation_replay_polls,maximum_candidate_end_media_lookahead_seconds,maximum_simulated_candidate_end_block_availability_to_commit_wall_seconds,maximum_commit_to_public_poll_wall_seconds,trace\n";
        std::cout<<"# runtime metrics: actual process clocks across all threads, never CPU inferred from work counts. Startup ends at first before_read; active ends before stop; join includes stop/destruction/trace finalization; posthoc is separate. Feed intervals exclude posthoc and are not isolated receiver-thread CPU. Simulated block availability is not hardware capture. Publication delay includes reported presentation-replay polls. Zero hardware queue backlog in simulation is not measured gain\n";
        std::cout<<"# payload-media window unavailable: the existing hook has no TX-owned physical payload start/end mapping through resampling/calendar/filters. Expected wire length or TX epoch is not substituted. No helper lead/tail samples enter a payload CPU/audio or wall/audio denominator. Raw actual feed intervals remain available for source-window review\n";
        std::cout<<"# decision timing uses the actual selected candidate physical endpoint for each commit which advances bit progress. A commit may publish several symbols together; these are progress-commit delays, not inferred individual per-symbol latency. Publication delay and candidate-end media lookahead remain separate; physical completion still requires the ordinary scored-END rules\n";
        std::cout<<"# runtime metric storage: bounded test-only heap bytes="<<RuntimeMetrics::capacity*(sizeof(RuntimeMetrics::Feed)+sizeof(RuntimeMetrics::Poll))<<"; observer overflow makes timing evidence unavailable and never truncates ordinary payload\n";
    }
    if(options.gpsdo_reference) {
        std::cout<<"# reference_configuration columns: profile,control,matched_workspace_bytes,LF_accuracy_ppm,RF_accuracy_ppm,modeled_CFO_Hz,modeled_audio_clock_ppm,modeled_phase_diffusion_degrees_per_sqrt_second\n";
        if(options.gpsdo_only)
            std::cout<<"# reference_control_only columns: profile,payload,GPSDO_reference_OFF_exit,workspace_bytes; declared OFF/ON runs are unexecuted, not inferred or reported as passed;0=passed,1=failed,2=incomplete\n";
        else
            std::cout<<"# reference_triplet columns: profile,payload,declared_OFF_exit,declared_ON_exit,GPSDO_reference_OFF_exit,identical_actual_workspace,workspace_bytes; every exit retains the complete ordinary source/wire/progress/physical-END checks;0=passed,1=failed,2=incomplete\n";
    }
    std::cout<<"# acceptance_configuration columns: profile,helper,configured_ON_helper_W,actual_helper_W,Controller_formatted_launch_command; new HF profiles retain Auto/rolling, named-key encryption and the declared shared/independent oscillator topology\n";
    std::cout<<"# acceptance_eligibility columns: profile,helper,ordinary_compact_requested,ordinary_numeric_grant_bytes,supported,backend,full_frequency_coverage,full_clock_coverage,reason; ordinary unassisted metadata only, never measured helper qualification or a constructed receiver\n";
    std::cout<<"# acceptance_resolved_geometry columns: profile,helper,actual_Fs,factory_Fs,actual_Rate,actual_SF,factory_SF,actual_symbol_samples,factory_symbol_samples,actual_DSSS,actual_FHSS,actual_deferred_symbols,factory_deferred_symbols,actual_LF_ppm,actual_RF_ppm,actual_RF_shift_Hz,actual_CFO_Hz,actual_audio_ppm; emitted before the HF FHSS profile assertion\n";
    if(options.observe_samples) {
        std::cout<<"# observation_window columns: profile,helper,requested_captured_samples,frontier_reached,simulation_finished,exit_code,outcome,captured_samples,captured_seconds,current_initial_FFT_samples,maximum_initial_FFT_samples,clock_window_modeled,traced_selection_backends,numeric_engine,current_helper_measurement,ever_helper_measurement,last_measurement_poll_captured_sample,last_reported_helper_SNR_10Hz_dB,last_reported_helper_frequency_offset_Hz,reference_valid,helper_CN0_dBHz,frequency_uncertainty_Hz,timing_valid,current_frequency_assistance_active,current_phase_assistance_active,ever_frequency_assistance_active,ever_phase_assistance_active,reported_original_pairs,reported_selected_pairs,current_reported_conditional_jobs,maximum_reported_conditional_jobs,current_reported_audit_jobs,traced_template_jobs,traced_conditional_jobs,traced_audit_jobs,receiver_instances,receiver_admissions,receiver_retirements,input_overruns,dropped_samples,input_interrupted,receiver_reset,search_limited,helper_reason,receiver_eligibility,status,error,trace\n";
        std::cout<<"# manual observation stops on a public captured-media frontier or the explicit wall deadline; neither supplies receiver timing/length/completion authority. Unavailable precision/reference/timing/CN0 and numeric-engine fields are not inferred from presence, flags, selection labels or planner geometry\n";
    }
    for(const auto profile:options.profiles) {
        std::optional<Result> ordinary;
        std::optional<Result> assisted;
        std::optional<Resolved> reference_base;
        std::optional<std::size_t> matched_workspace=options.workspace_bytes;
        for(const bool helper:options.helpers) {
            auto resolved=resolve(profile,helper,options);
            const auto controller_workspace=resolved.settings.dsp_workspace_bytes;
            require(resolved.settings.transfer.dsp_workspace_bytes==controller_workspace,
                    "Controller session and transfer workspace budgets unexpectedly differ");
            // Resolve the percentage once for this paired experiment. Later
            // MemAvailable fluctuations must not change its memory control.
            if(!matched_workspace)matched_workspace=controller_workspace;
            resolved.settings.dsp_workspace_bytes=resolved.settings.transfer.dsp_workspace_bytes=*matched_workspace;
            if(options.gpsdo_reference&&!helper)reference_base=resolved;
            const auto bank_bytes=live::receiver_workspace_capacity(resolved.settings);
            require(bank_bytes>0,"matched workspace has no ordinary receiver capacity");
            std::cout<<"workspace,"<<name(profile)<<','<<helper<<','<<resolved.patch.workspace_percent.value_or(0)<<','
                <<controller_workspace<<','<<*matched_workspace<<','<<bank_bytes<<','
                <<(options.workspace_bytes?"explicit identical byte budget":"% resolved once per matched pair")<<'\n';
            metadata(profile,helper,resolved);
            // A standalone control clones the identical declared OFF geometry
            // below, without repeating already preserved OFF/ON experiments.
            if(options.metadata_only||options.gpsdo_only)continue;
            auto result=run(profile,helper,resolved,options);
            if(result.exit==1)status=1;else if(result.exit==2&&status==0)status=2;
            if(!helper)ordinary=result;
            else if(options.gpsdo_reference)assisted=result;
            if(helper&&ordinary&&ordinary->transmit_epoch&&result.transmit_epoch) {
                std::cout<<"matched,"<<name(profile)<<','<<name(options.payload)<<','
                    <<ordinary->transmit_epoch<<','<<result.transmit_epoch<<','
                    <<(ordinary->dated_wire==result.dated_wire)<<','
                    <<(ordinary->exit==0&&result.exit==0)<<','
                    <<ordinary->template_jobs<<','<<result.template_jobs<<','<<result.conditional_jobs<<','<<result.audit_jobs<<','
                    <<(ordinary->work_traced&&result.work_traced)<<','
                    <<(ordinary->exit==0&&result.exit==0&&ordinary->work_traced&&result.work_traced&&
                        result.template_jobs<ordinary->template_jobs)<<'\n';
                std::cout<<"matched_compact,"<<name(profile)<<','<<name(options.payload)<<','
                    <<ordinary->transmit_epoch<<','<<result.transmit_epoch<<','<<(ordinary->exit==0&&result.exit==0)<<','
                    <<ordinary->compact_work_traced<<','<<result.compact_work_traced<<','
                    <<ordinary->compact.instances<<','<<result.compact.instances;
                for(const auto counter:ordinary->compact.samples)std::cout<<','<<counter;
                for(const auto counter:result.compact.samples)std::cout<<','<<counter;
                const bool both_work=ordinary->compact_work_traced&&result.compact_work_traced;
                std::cout<<','<<(both_work&&result.compact.samples[lane_samples]<ordinary->compact.samples[lane_samples])<<','
                    <<(both_work&&result.compact.samples[projected_samples]<ordinary->compact.samples[projected_samples])<<','
                    <<ordinary->cpu_seconds<<','<<result.cpu_seconds<<','<<ordinary->wall_seconds<<','<<result.wall_seconds<<','
                    <<ordinary->maximum_backlog_seconds<<','<<result.maximum_backlog_seconds<<','
                    <<ordinary->captured_samples<<','<<result.captured_samples<<'\n';
                std::cout<<"matched_input_views,"<<name(profile)<<','<<name(options.payload)<<','
                    <<(ordinary->exit==0&&result.exit==0)<<','<<ordinary->input_view_jobs<<','<<result.input_view_jobs<<','
                    <<ordinary->unique_conditional_jobs<<','<<result.unique_conditional_jobs<<','
                    <<ordinary->template_jobs<<','<<result.template_jobs<<','<<result.primary_input_view_certified<<','
                    <<result.later_input_view_assisted<<'\n';
                std::cout<<"matched_native_arithmetic,"<<name(profile)<<','<<name(options.payload)<<','
                    <<(ordinary->exit==0&&result.exit==0)<<','<<ordinary->native_arithmetic.complete<<','
                    <<result.native_arithmetic.complete<<','<<ordinary->native_arithmetic.instances<<','<<result.native_arithmetic.instances;
                print_native_arithmetic(ordinary->native_arithmetic);print_native_arithmetic(result.native_arithmetic);
                std::cout<<','<<ordinary->template_jobs<<','<<result.template_jobs<<','<<ordinary->cpu_seconds<<','<<result.cpu_seconds<<','
                    <<ordinary->wall_seconds<<','<<result.wall_seconds<<','<<ordinary->maximum_backlog_seconds<<','
                    <<result.maximum_backlog_seconds<<','<<ordinary->captured_samples<<','<<result.captured_samples<<','
                    <<ordinary->workspace_bytes<<','<<result.workspace_bytes<<','
                    <<(ordinary->workspace_bytes==result.workspace_bytes)<<'\n';
            }
        }
        if(options.gpsdo_reference) {
            require(reference_base.has_value()&&matched_workspace.has_value(),"GPSDO reference lost its declared helper-OFF base");
            auto resolved=gpsdo_reference(profile,std::move(*reference_base));
            require(resolved.settings.dsp_workspace_bytes==*matched_workspace&&
                    resolved.settings.transfer.dsp_workspace_bytes==*matched_workspace,
                    "GPSDO reference changed the explicit matched byte workspace");
            std::cout<<"# GPSDO simulation reference follows: cloned declared geometry, helper OFF, modeled prior/truth change only; no GPS clock sync, known RX epoch or physical GPSDO hardware claim\n";
            std::cout<<"reference_configuration,"<<name(profile)<<",gpsdo-simulation-reference,"
                <<*matched_workspace<<','<<resolved.settings.transfer.modem.oscillator_search->lf.accuracy_ppm<<','
                <<resolved.settings.transfer.modem.oscillator_search->rf.accuracy_ppm<<','
                <<resolved.settings.simulation_frequency_offset_hz<<','<<resolved.settings.simulation_clock_error_ppm<<','
                <<resolved.settings.simulation_phase_noise_degrees_per_sqrt_second<<'\n';
            metadata(profile,false,resolved);
            if(!options.metadata_only) {
                auto result=run(profile,false,resolved,options);
                if(result.exit==1)status=1;else if(result.exit==2&&status==0)status=2;
                if(options.gpsdo_only)
                    std::cout<<"reference_control_only,"<<name(profile)<<','<<name(options.payload)<<','
                        <<result.exit<<','<<result.workspace_bytes<<'\n';
                else
                    std::cout<<"reference_triplet,"<<name(profile)<<','<<name(options.payload)<<','
                        <<(ordinary?ordinary->exit:2)<<','<<(assisted?assisted->exit:2)<<','<<result.exit<<','
                        <<(ordinary&&assisted&&ordinary->workspace_bytes==result.workspace_bytes&&
                            assisted->workspace_bytes==result.workspace_bytes)<<','<<result.workspace_bytes<<'\n';
            }
        }
    }
    if(!options.metadata_only)
        std::cout<<"# matched columns: profile,payload,OFF_TX_epoch,ON_TX_epoch,same_dated_wire,both_full_source_exact,OFF_observed_jobs,ON_observed_jobs,ON_conditional_jobs,ON_audit_jobs,both_work_traced,full_decode_with_fewer_observed_jobs; helper lead may legitimately change the future key epoch and wire\n";
    if(!options.metadata_only)
        std::cout<<"# matched_compact columns: profile,payload,OFF_TX_epoch,ON_TX_epoch,both_full_source_exact,OFF_compact_work_traced,ON_compact_work_traced,OFF_compact_instances,ON_compact_instances,OFF_projected_samples,OFF_lane_samples,OFF_skipped_projected_samples,OFF_skipped_lane_samples,OFF_audit_projected_samples,OFF_audit_lane_samples,ON_projected_samples,ON_lane_samples,ON_skipped_projected_samples,ON_skipped_lane_samples,ON_audit_projected_samples,ON_audit_lane_samples,fewer_executed_lane_samples,fewer_local_projected_samples,OFF_process_CPU_seconds,ON_process_CPU_seconds,OFF_wall_seconds,ON_wall_seconds,OFF_maximum_backlog_seconds,ON_maximum_backlog_seconds,OFF_captured_samples,ON_captured_samples; each run retains its actual full/partial result and media window, no compact work is added to native FFT jobs\n";
    if(!options.metadata_only)
        std::cout<<"# matched_input_views columns: profile,payload,both_full_source_exact,OFF_view_native_jobs,ON_view_native_jobs,OFF_unique_conditional_native_jobs,ON_unique_conditional_native_jobs,OFF_total_native_jobs,ON_total_native_jobs,ON_primary_wire_view_certified,ON_later_view_assisted_epoch; view native jobs are already counted once in total_native_jobs\n";
    if(!options.metadata_only) {
        std::cout<<"# matched_native_arithmetic columns: profile,payload,both_full_source_exact,OFF_complete_counter_coverage,ON_complete_counter_coverage,OFF_traced_counter_instances,ON_traced_counter_instances,OFF_fft_jobs,OFF_direct_jobs,OFF_exact_reused_template_jobs,OFF_partitioned_transform_points,OFF_native_fp32_transforms,OFF_tabulated_fp64_transforms,ON_fft_jobs,ON_direct_jobs,ON_exact_reused_template_jobs,ON_partitioned_transform_points,ON_native_fp32_transforms,ON_tabulated_fp64_transforms,OFF_template_jobs,ON_template_jobs,OFF_process_CPU_seconds,ON_process_CPU_seconds,OFF_wall_seconds,ON_wall_seconds,OFF_maximum_backlog_seconds,ON_maximum_backlog_seconds,OFF_captured_samples,ON_captured_samples,OFF_DSP_workspace_bytes,ON_DSP_workspace_bytes,same_DSP_workspace; raw counters have distinct units and imply no combined arithmetic cost\n";
        std::cout<<"# Matched pairs resolve percentage workspace once within this invocation. Separate helper-on invocations re-resolve that percentage; cross-candidate CPU comparisons are not memory-controlled merely because their percentage arguments match\n";
    }
    if(options.metadata_only)std::cout<<"# metadata_only: no receiver bank, waveform, acquisition, completion or throughput was executed\n";
    return status;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
