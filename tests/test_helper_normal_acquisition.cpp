#include "../src/gui/controller.hpp"
#include "../src/gui/launch_command.hpp"
#include "../src/live_receiver_admission.hpp"
#include "../src/live_helper_prior.hpp"
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
enum class Profile {hf,eme,practical,fast_auto,long_auto};
enum class Payload {bits,text,attachment};
const char* name(Profile p) {return p==Profile::hf?"hf":p==Profile::eme?"eme":p==Profile::practical?"practical":p==Profile::fast_auto?"fast-auto":"long-auto";}
const char* name(Payload p) {return p==Payload::bits?"bits":p==Payload::text?"text":"attachment";}
struct Options {
    std::vector<Profile> profiles{Profile::hf,Profile::eme};
    std::vector<bool> helpers{false,true};
    Payload payload=Payload::bits;
    bool metadata_only=false,ephemeral_key=false,keyfile_explicit=false,bit_count_explicit=false,wall_explicit=false;
    std::optional<unsigned> source_bytes;
    std::optional<std::uint64_t> observe_samples;
    std::optional<std::filesystem::path> inspect_path;
    std::optional<std::uint64_t> inspect_epoch;
    std::filesystem::path keyfile="/tmp/demoKey",evidence;
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
        else if(argument=="--ephemeral-key")result.ephemeral_key=true;
        else if(argument=="--hf")add_profile(Profile::hf);
        else if(argument=="--eme")add_profile(Profile::eme);
        else if(argument=="--practical")add_profile(Profile::practical);
        else if(argument=="--fast-auto")add_profile(Profile::fast_auto);
        else if(argument=="--long-auto")add_profile(Profile::long_auto);
        else if(argument=="--keyfile") {result.keyfile=value();result.keyfile_explicit=true;}
        else if(argument=="--evidence-dir")result.evidence=value();
        else if(argument=="--inspect-trace")result.inspect_path=value();
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
std::vector<std::string> launch_arguments(Profile profile,bool helper,const std::filesystem::path& keyfile) {
    // HF and EME retain the user's complete commands. Practical is a separate
    // diagnostic profile. Only helper power differs within a matched pair;
    // simulation is selected by Controller::Options.
    std::vector<std::string> args{
        "--pattern","auto-keystream","--tx-dbm","36.020599913279625",
        "--helper-power",helper?"1500.0000000000005":"0","--helper-ratio-limit","90",
        "--helper-frequency","high","--helper-callsign=","--noise-dbm-hz","-164",
        "--search-margin","3","--reference","independent","--doppler","0.000000c",
        "--clock-sync","default","--audio-error","0ms","--dsss-factor","10",
        "--dsss-version","interleave","--search-arithmetic","default",
        "--receiver-strategy","auto","--receiver-cpu-budget","0.6","--live-duplex","yes",
        "--sched-hop-dilution","0","--sched-hop","0","--sched-frequency-search","1",
        "--symbol-burst","off","--symbol-burst-random","0","--keyfile="+keyfile.string(),
        "--key-name=Default","--tx-key","named"};
    const std::vector<std::string> specific=profile==Profile::hf?std::vector<std::string>{
        "--path-loss-db","180","--oscillator","crystal","--rf-oscillator","ic-7100",
        "--shift","3e+07","--target-snr","8","--rate","36","--carrier","3.00015e+07",
        "--dsp-workspace","50%","--dsss-mode","interleave","--fhss","off"}:
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
};
struct PriorGeometry {
    std::size_t clocks=0,selected=0,represented_clocks=0;
    double reference_uncertainty=.5,offset=0,radius=0,clock_cell_half_width_ppm=0;
    bool admitted=false;
};
PriorGeometry predicted_prior_geometry(const live::Settings& value,const modem::OscillatorPatternSearch& search) {
    PriorGeometry result;const auto& c=value.transfer.modem;
    auto options=value.helper;options.power_w=1500;
    const auto width=std::max(helper::occupied_bandwidth(c),helper::occupied_bandwidth(*value.long_message_modem));
    const auto plan=helper::plan(options,c.carrier_hz,width,120000000,0,modem::outer_dsss_transmit_gain(c));
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
void auto_geometry(Profile profile,const live::Settings& value,bool helper_enabled) {
    const auto& c=value.transfer.modem;
    const auto domain=modem::oscillator_pattern_search(c,
        transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(c):0);
    const auto prior=predicted_prior_geometry(value,domain);
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
    auto options=value.helper;options.power_w=1500;
    const auto width=std::max(helper::occupied_bandwidth(c),helper::occupied_bandwidth(*value.long_message_modem));
    const auto tone=helper::plan(options,c.carrier_hz,width,120000000,0,modem::outer_dsss_transmit_gain(c)).frequency_hz;
    helper::PayloadFilter filter(c.sample_rate,tone);
    constexpr auto reserved=modem::SampledSimulationChannel::helper_cohort_workspace_bound;
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
                low>0&&high<seeded.sample_rate/2.&&(high<tone-325||low>tone+325)&&error<=.001,
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
    auto arguments=launch_arguments(profile,helper,options.keyfile);
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
    require(close(settings.helper.power_w,helper?1500.:0.)&&close(settings.helper.ratio_limit_db,90)&&
            settings.helper.frequency==helper::Frequency::high&&settings.helper.callsign.empty(),
            "Controller changed the helper power, ratio, placement or empty callsign");
    const auto& c=settings.transfer.modem;
    require(c.oscillator_search&&c.oscillator_search->reference==modem::OscillatorReference::independent_audio&&
            close(c.oscillator_search->margin,3)&&close(c.carrier_hz,1500),
            "Controller changed independent clock topology, search margin or audio carrier");
    require(settings.long_message_modem.has_value(),"Controller omitted the configured long-message profile");
    if(profile==Profile::hf) {
        require(patch.workspace_percent==50&&close(*patch.target_db_hz,8)&&c.sample_rate==6120&&
                close(c.bandwidth_hz,36)&&c.spreading_factor==256&&c.dsss_factor==10&&
                c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2&&
                modem::symbol_sample_count(c)==87040&&!c.fake_hopping&&
                close(c.oscillator_search->lf.accuracy_ppm,100)&&close(c.oscillator_search->rf.accuracy_ppm,1)&&
                close(c.oscillator_search->rf_shift_hz,3e7),"HF command did not retain the user's exact physical profile");
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
        const auto prior=predicted_prior_geometry(settings,search);
        require(!search.limited&&search.hypotheses.size()==49&&prior.clocks==3&&
                close(search.requested_clock_half_width_ppm,300)&&close(search.clock_half_width_ppm,300)&&
                prior.admitted&&prior.selected>0&&prior.selected<search.hypotheses.size()&&prior.represented_clocks==prior.clocks,
                "practical original catalogue lacks complete coverage or cannot narrow while retaining every clock lane");
    }
    // Fix only the random channel draw. Every physical impairment and level
    // remains the actual Controller result of the supplied command.
    settings.simulation_seed=5181;
    return {std::move(settings),patch,std::move(arguments)};
}
void metadata(Profile profile,bool helper,const Resolved& resolved) {
    const auto& value=resolved.settings;const auto& c=value.transfer.modem;
    if(profile==Profile::fast_auto||profile==Profile::long_auto)auto_geometry(profile,value,helper);
    const auto search=modem::oscillator_pattern_search(c,
        transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(c):0);
    std::set<double> rates,frequencies;for(const auto& pair:search.hypotheses) {rates.insert(pair.clock_error_ppm);frequencies.insert(pair.frequency_offset_hz);}
    std::cout<<std::setprecision(17)<<"metadata,"<<name(profile)<<','<<helper<<','<<c.sample_rate<<','<<c.bandwidth_hz<<','
             <<c.spreading_factor<<','<<c.dsss_factor<<','<<modem::symbol_sample_count(c)<<','
             <<static_cast<double>(modem::symbol_sample_count(c))/c.sample_rate<<','<<search.hypotheses.size()<<','
             <<frequencies.size()<<','<<rates.size()<<','<<search.frequency.requested_half_width_hz<<','
             <<search.frequency.half_width_hz<<','<<search.limited<<','<<value.dsp_workspace_bytes<<','
             <<live::receiver_workspace_capacity(value)<<','<<value.simulation_snr_db<<','
             <<value.simulation_frequency_offset_hz<<','<<value.simulation_clock_error_ppm<<','
             <<value.simulation_phase_noise_degrees_per_sqrt_second<<','<<value.receive_keys.size()<<'\n';
    if(profile==Profile::practical||profile==Profile::fast_auto||profile==Profile::long_auto) {
        const auto prior=predicted_prior_geometry(value,search);
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
struct Dates {
    std::atomic<std::uint64_t> transmit{0};
    WireCollector* collector=nullptr;
    live::detail::SimulationHook hook{this,[](void* opaque,const live::detail::SimulationObservation& observation) {
        if(observation.phase==live::detail::SimulationPhase::transmit_epoch)
            static_cast<Dates*>(opaque)->transmit=static_cast<std::uint64_t>(observation.epoch);
    },1800000000.125};
    explicit Dates(WireCollector* wire=nullptr):collector(wire) {
        if(collector)hook.wire_callback=[](void* opaque,const live::detail::ReceiverWireObservation& observation)noexcept {
            static_cast<Dates*>(opaque)->collector->observe(observation);
        };
        live::detail::SimulationHook* empty=nullptr;
        require(live::detail::simulation_hook.compare_exchange_strong(empty,&hook),"simulation date hook already installed");
    }
    ~Dates(){live::detail::simulation_hook.store(nullptr);}
};
std::atomic<bool> trace_detail{true};
bool trace_filter(const char* event) noexcept {
    const std::string_view name{event};
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
struct TraceResult {
    bool pending=false,complete=false,observed_end=false,identity=true,prefix=true,assisted=false,phase=false,later_epoch=false;
    bool all_rows_end=true;
    std::uint64_t id=0,revision=0,bits=0;
    std::uint64_t rows=0,secondary_rows=0;
    bool wire_exact=false,wire_contiguous=false,wire_complete=false;
    std::uint64_t wire_bits=0;
    std::uint64_t work_instances=0,template_jobs=0,input_transforms=0,conditional_jobs=0,audit_jobs=0;
    bool cohort_frontend=false,cohort_phase=false,cohort_resources_valid=true;
    std::uint64_t cohort_peak_bytes=0,cohort_reserve_bytes=0;
    bool trace_complete=false;
    bool wire_evidence_complete=true,wire_assisted=false;
    std::set<std::string> selection_backends;
};
TraceResult inspect_trace(const std::filesystem::path& path,const Bytes& expected,const Bytes& dated_wire,std::uint32_t rate,
                          const WireCollector* collector=nullptr) {
    TraceResult result;result.wire_evidence_complete=!collector||collector->complete();
    result.trace_complete=trace_integrity(path);
    // Missing trace data cannot establish payload errors, progress or END.
    // No incomplete evidence is used for qualification or work accounting.
    if(!result.trace_complete)return result;
    std::ifstream input(path);std::string line;std::set<std::uint64_t> initial_epochs;
    std::map<std::array<std::uint64_t,3>,std::array<std::uint64_t,4>> work;
    std::map<std::array<std::uint64_t,2>,RowProgress> rows;
    struct Wire {
        std::uint64_t instance=0,first_sample=0,first_symbol=0,epoch=0;
        bool initialized=false,contiguous=true,consistent=true,complete=false,observed_end=false;
        std::string bits;
    };
    std::map<std::array<std::uint64_t,2>,Wire> wires;
    constexpr std::size_t trace_bit_capacity=32*1024*1024;
    std::size_t trace_bits=0;
    const auto expected_text=binary(expected),expected_wire=binary(dated_wire);
    while(std::getline(input,line)) {
        if(line.find("\"event\":\"receiver_selection\"")!=std::string::npos)
            if(const auto backend=string_field(line,"backend"))result.selection_backends.insert(*backend);
        const bool frontend=line.find("\"event\":\"receiver_helper_cohort_frontend\"")!=std::string::npos;
        const bool phase_storage=line.find("\"event\":\"receiver_helper_cohort_phase_storage\"")!=std::string::npos;
        if(frontend||phase_storage) {
            const auto actual=number(line,"actual_bytes"),reserved=number(line,"reserved_bytes");
            require(actual&&reserved,"helper cohort trace omitted its actual allocation or reserve");
            result.cohort_frontend=result.cohort_frontend||frontend;
            result.cohort_phase=result.cohort_phase||phase_storage;
            result.cohort_resources_valid=result.cohort_resources_valid&&*actual<=*reserved&&
                *reserved<=modem::SampledSimulationChannel::helper_cohort_workspace_bound;
            result.cohort_peak_bytes=std::max(result.cohort_peak_bytes,*actual);
            result.cohort_reserve_bytes=result.cohort_reserve_bytes?std::min(result.cohort_reserve_bytes,*reserved):*reserved;
        }
        if(line.find("\"event\":\"receiver_pattern_wire\"")!=std::string::npos) {
            const auto capture=number(line,"input_revision"),candidate=number(line,"candidate_id"),
                instance=number(line,"instance_id"),epoch=number(line,"epoch"),first_sample=number(line,"stream_first_sample"),
                first_symbol=number(line,"stream_first_symbol"),ordinal=number(line,"first_stream_symbol"),missing=number(line,"missing_slots");
            auto bits=string_field(line,"bits");
            require(capture&&candidate&&instance&&epoch&&first_sample&&first_symbol&&ordinal&&missing&&bits,
                    "physical wire trace omitted its actual stream identity, ordinal or bits");
            require(bits->size()<=1024&&bits->find_first_not_of("012")==std::string::npos&&
                    (!*missing||bits->empty()),"physical wire trace violated its bounded piece contract");
            auto& wire=wires[{*capture,*candidate}];
            if(!wire.initialized) {
                wire.initialized=true;wire.instance=*instance;wire.epoch=*epoch;
                wire.first_sample=*first_sample;wire.first_symbol=*first_symbol;
            }
            require(wire.instance==*instance&&wire.epoch==*epoch&&wire.first_sample==*first_sample&&
                    wire.first_symbol==*first_symbol&&*ordinal>=*first_symbol,
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
        }
        if(line.find("\"event\":\"receiver_acquisition_work\"")!=std::string::npos) {
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
        }
        if(line.find("\"event\":\"receiver_bank_admitted\"")!=std::string::npos) {
            const auto epoch=number(line,"epoch"),sample=number(line,"sample");
            if(epoch&&sample) {if(*sample==0)initial_epochs.insert(*epoch);else if(!initial_epochs.contains(*epoch))result.later_epoch=true;}
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
        result.pending=primary->pending;result.complete=primary->complete&&primary->bits==expected.size();
        result.observed_end=primary->observed_end;result.assisted=primary->assisted;
        if(collector) {
            if(const auto* wire=collector->find(primary_key[0],primary->candidate)) {
                result.wire_bits=wire->size;
                result.wire_exact=wire->size==dated_wire.size()&&
                    (!wire->size||std::equal(wire->bits.get(),wire->bits.get()+wire->size,dated_wire.begin()));
                result.wire_contiguous=wire->contiguous&&wire->consistent;
                result.wire_complete=wire->complete&&wire->observed_end;
                result.wire_assisted=wire->helper_acquired;
            }
        } else if(const auto found=wires.find({primary_key[0],primary->candidate});found!=wires.end()) {
            const auto& wire=found->second;result.wire_bits=wire.bits.size();
            result.wire_exact=wire.bits==expected_wire;
            result.wire_contiguous=wire.contiguous&&wire.consistent;
            result.wire_complete=wire.complete&&wire.observed_end;
        }
    }
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
    }
    return result;
}
struct Result {
    int exit=2;
    Bytes dated_wire;
    std::uint64_t transmit_epoch=0;
    std::uint64_t template_jobs=0,conditional_jobs=0,audit_jobs=0;
    bool work_traced=false;
};
struct PublicHelperObservation {
    bool ever_measured=false,ever_frequency_active=false,ever_phase_active=false;
    std::optional<double> last_snr,last_frequency_offset;
    std::uint64_t last_measured_at=0,maximum_initial_fft=0;
    void observe(const live::Snapshot& snapshot) {
        ever_frequency_active=ever_frequency_active||snapshot.helper_frequency_assistance_active;
        ever_phase_active=ever_phase_active||snapshot.helper_phase_assistance_active;
        maximum_initial_fft=std::max(maximum_initial_fft,snapshot.receiver_initial_fft_input_samples);
        if(snapshot.helper_snr_db&&snapshot.helper_frequency_offset_hz) {
            ever_measured=true;last_snr=snapshot.helper_snr_db;last_frequency_offset=snapshot.helper_frequency_offset_hz;
            // Poll frontier only; Snapshot does not expose the actual raw
            // reference measurement date or its age.
            last_measured_at=snapshot.samples_received;
        }
    }
};
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
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    std::filesystem::create_directories(path.parent_path());
    std::atomic<std::int64_t> replay_ms{0};Bytes expected,dated_wire;
    live::Snapshot last;bool finished=false,exact=false,runtime_error=false,observation_reached=false;
    PublicHelperObservation helper_observation;
    std::uint64_t transmit_epoch=0,max_workspace=0;
    std::uint64_t maximum_snapshot_conditional_jobs=0;
    const bool detailed=options.payload==Payload::bits;
    WireCollector collector(c.sample_rate);
    trace_detail.store(detailed,std::memory_order_relaxed);
    const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();
    {
        audio::trace::Scope trace(path,false,&trace_filter);Dates dates(detailed?nullptr:&collector);
        // Empty EpochClock retains the normal sample-driven simulation path.
        live::Session session({},[&]{return std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(replay_ms.load());});
        session.start(value);
        if(options.payload==Payload::bits)session.transmit_bits(known_bits(options.bit_count));else session.transmit(message);
        const auto deadline=start+std::chrono::seconds(options.wall_seconds);
        while(std::chrono::steady_clock::now()<deadline) {
            last=session.snapshot();max_workspace=std::max<std::uint64_t>(max_workspace,last.dsp_buffered_bytes);
            if(options.observe_samples)helper_observation.observe(last);
            maximum_snapshot_conditional_jobs=std::max(maximum_snapshot_conditional_jobs,last.helper_conditional_jobs);
            if(!last.error.empty()||last.receiver_health.receiver_reset||last.receiver_health.input_interrupted||
                last.receiver_health.input_overruns||last.receiver_health.dropped_samples) {runtime_error=true;break;}
            if(last.transmission_finished&&last.simulation_replay) {
                replay_ms=4000;finished=true;last=session.snapshot();
                if(options.observe_samples)helper_observation.observe(last);
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
        transmit_epoch=dates.transmit.load();session.stop();
    }
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
    const auto observed=inspect_trace(path,expected,dated_wire,c.sample_rate,detailed?nullptr:&collector);
    const auto assistance_jobs=detailed?observed.conditional_jobs:maximum_snapshot_conditional_jobs;
    std::cout<<"assistance_evidence,"<<name(profile)<<','<<helper<<','<<(detailed?"detailed_bits":"light_framed")<<','
        <<(detailed?"traced_per_instance":"maximum_public_snapshot")<<','<<assistance_jobs<<','
        <<maximum_snapshot_conditional_jobs<<','<<observed.trace_complete<<','<<observed.wire_evidence_complete<<','
        <<observed.wire_assisted<<'\n';
    // Only the new long Auto helper scenario requires actual assisted-lane
    // evidence. Metadata cannot prove the private control allocation or
    // received-reference admission, and an ordinary-only decode is insufficient.
    const bool assistance_required=profile==Profile::long_auto&&helper;
    const bool assistance_qualified=!assistance_required||(observed.cohort_frontend&&
        observed.cohort_resources_valid&&assistance_jobs>0&&observed.assisted&&(detailed||observed.wire_assisted));
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
             <<static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC<<','
             <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<','<<path.string()<<'\n';
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
    return {exit,std::move(dated_wire),transmit_epoch,observed.template_jobs,
        observed.conditional_jobs,observed.audit_jobs,detailed&&observed.trace_complete&&observed.work_instances!=0&&!runtime_error};
}
int reinspect(const Options& options,const Resolved& resolved) {
    // Analysis of a pre-existing, sealed binary-wire trace only. The supplied
    // actual TX date is used after capture to reconstruct its expected mask.
    // No Session is started; no receiver, PCM, source decoder or capture is created.
    const auto profile=options.profiles.front();const auto helper=options.helpers.front();
    const auto expected=known_bits(options.bit_count);auto dated=resolved.settings.transfer;
    dated.timestamp=*options.inspect_epoch;auto wire=expected;transfer::xor_binary_bits(wire,dated);
    const auto observed=inspect_trace(*options.inspect_path,expected,wire,dated.modem.sample_rate);
    const bool assisted=profile!=Profile::long_auto||!helper||(observed.cohort_frontend&&
        observed.cohort_resources_valid&&observed.conditional_jobs>0&&observed.assisted);
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
    std::cout<<"# posthoc_reinspection checks the original sealed trace only; it does not rerun reception, Snapshot source admission or runtime health, and never replaces the original run outcome\n";
    return status;
}
}
int main(int argc,char** argv) {try {
    auto options=parse(argc,argv);row_progress_guards();EphemeralKey key(options);int status=0;
    if(options.inspect_path) {
        std::cout<<"# posthoc_reinspection columns: profile,helper,actual_TX_epoch,actual_bit_count,trace_result,primary_id,primary_revision,primary_bits,primary_wire_bits,pending,complete,scored_END,wire_equal,wire_contiguous,wire_complete,revision_identity,primary_lifetime_prefix,all_historical_completed_END,rows,primary_assisted,original_trace\n";
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
    std::cout<<"# assistance_evidence columns: profile,helper,trace_mode,conditional_count_source,conditional_jobs_for_assistance_gate,maximum_public_snapshot_conditional_jobs,complete_trace,complete_wire_evidence,primary_wire_helper_acquired; light framed mode uses the bounded typed wire observer and does not measure total per-instance work or qualify work savings\n";
    std::cout<<"# wire_collector columns: trace,complete_evidence,failure,observations,candidates,logical_bit_bytes,current_data_capacity_bytes,peak_data_capacity_bytes_including_transient_growth,data_capacity_bound_bytes,fixed_candidate_metadata_bytes,collector_control_bytes,candidate_capacity; diagnostic element capacities only, never a DSP budget claim\n";
    std::cout<<"# observed work sums each traced instance's maximum cumulative execution counters; no work is inferred from search-pair counts or absent trace records\n";
    std::cout<<"# prior_geometry columns: profile,helper,original_pairs,predicted_selected_pairs,original_clock_lanes,represented_clock_lanes,assumed_reference_uncertainty_Hz,predicted_helper_offset_Hz,predicted_mask_radius_Hz,authoritative_clock_cell_half_width_ppm,predicted_prior_admitted; shared-selector geometry only, not a received measurement\n";
    std::cout<<"# workspace columns: profile,helper,requested_percent,Controller_resolved_bytes,matched_workspace_bytes,matched_bank_bytes,policy\n";
    if(options.observe_samples) {
        std::cout<<"# observation_window columns: profile,helper,requested_captured_samples,frontier_reached,simulation_finished,exit_code,outcome,captured_samples,captured_seconds,current_initial_FFT_samples,maximum_initial_FFT_samples,clock_window_modeled,traced_selection_backends,numeric_engine,current_helper_measurement,ever_helper_measurement,last_measurement_poll_captured_sample,last_reported_helper_SNR_10Hz_dB,last_reported_helper_frequency_offset_Hz,reference_valid,helper_CN0_dBHz,frequency_uncertainty_Hz,timing_valid,current_frequency_assistance_active,current_phase_assistance_active,ever_frequency_assistance_active,ever_phase_assistance_active,reported_original_pairs,reported_selected_pairs,current_reported_conditional_jobs,maximum_reported_conditional_jobs,current_reported_audit_jobs,traced_template_jobs,traced_conditional_jobs,traced_audit_jobs,receiver_instances,receiver_admissions,receiver_retirements,input_overruns,dropped_samples,input_interrupted,receiver_reset,search_limited,helper_reason,receiver_eligibility,status,error,trace\n";
        std::cout<<"# manual observation stops on a public captured-media frontier or the explicit wall deadline; neither supplies receiver timing/length/completion authority. Unavailable precision/reference/timing/CN0 and numeric-engine fields are not inferred from presence, flags, selection labels or planner geometry\n";
    }
    for(const auto profile:options.profiles) {
        std::optional<Result> ordinary;
        std::optional<std::size_t> matched_workspace;
        for(const bool helper:options.helpers) {
            auto resolved=resolve(profile,helper,options);
            const auto controller_workspace=resolved.settings.dsp_workspace_bytes;
            require(resolved.settings.transfer.dsp_workspace_bytes==controller_workspace,
                    "Controller session and transfer workspace budgets unexpectedly differ");
            // Resolve the percentage once for this paired experiment. Later
            // MemAvailable fluctuations must not change its memory control.
            if(!matched_workspace)matched_workspace=controller_workspace;
            resolved.settings.dsp_workspace_bytes=resolved.settings.transfer.dsp_workspace_bytes=*matched_workspace;
            const auto bank_bytes=live::receiver_workspace_capacity(resolved.settings);
            require(bank_bytes>0,"matched workspace has no ordinary receiver capacity");
            std::cout<<"workspace,"<<name(profile)<<','<<helper<<','<<resolved.patch.workspace_percent.value_or(0)<<','
                <<controller_workspace<<','<<*matched_workspace<<','<<bank_bytes<<",% resolved once per matched pair\n";
            metadata(profile,helper,resolved);
            if(options.metadata_only)continue;
            auto result=run(profile,helper,resolved,options);
            if(result.exit==1)status=1;else if(result.exit==2&&status==0)status=2;
            if(!helper)ordinary=result;
            else if(ordinary&&ordinary->transmit_epoch&&result.transmit_epoch)
                std::cout<<"matched,"<<name(profile)<<','<<name(options.payload)<<','
                    <<ordinary->transmit_epoch<<','<<result.transmit_epoch<<','
                    <<(ordinary->dated_wire==result.dated_wire)<<','
                    <<(ordinary->exit==0&&result.exit==0)<<','
                    <<ordinary->template_jobs<<','<<result.template_jobs<<','<<result.conditional_jobs<<','<<result.audit_jobs<<','
                    <<(ordinary->work_traced&&result.work_traced)<<','
                    <<(ordinary->exit==0&&result.exit==0&&ordinary->work_traced&&result.work_traced&&
                        result.template_jobs<ordinary->template_jobs)<<'\n';
        }
    }
    if(!options.metadata_only)
        std::cout<<"# matched columns: profile,payload,OFF_TX_epoch,ON_TX_epoch,same_dated_wire,both_full_source_exact,OFF_observed_jobs,ON_observed_jobs,ON_conditional_jobs,ON_audit_jobs,both_work_traced,full_decode_with_fewer_observed_jobs; helper lead may legitimately change the future key epoch and wire\n";
    if(options.metadata_only)std::cout<<"# metadata_only: no receiver bank, waveform, acquisition, completion or throughput was executed\n";
    return status;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
