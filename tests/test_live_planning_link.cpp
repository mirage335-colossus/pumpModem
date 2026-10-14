// This executable must link only datapump: no audio objects, policies, mocks,
// provider traps or stubs. Its successful link is the primary regression gate.
#include "datapump/live.hpp"
#include "datapump/runtime.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/simulation_estimate.hpp"
#include "../src/live_receiver_layout.hpp"
#include "../src/scheduled_acquisition_policy.hpp"
#include <cstdlib>
#include <optional>
#include <string>
#include <cmath>
#include <iostream>
#include <limits>

using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
void structural_normalization_and_reservations() {
    live::Settings legacy;
    legacy.transfer.dsp_workspace_bytes=512ULL*1024*1024;
    auto explicit_settings=legacy;explicit_settings.dsp_workspace_bytes=legacy.transfer.dsp_workspace_bytes;
    const auto capacity=live::receiver_workspace_capacity(legacy);
    check(capacity==live::receiver_workspace_capacity(explicit_settings),
        "legacy transfer workspace must normalize exactly like explicit Live workspace");
    const auto normalized=live::detail::normalized_layout(legacy);
    check(normalized.dsp_workspace_bytes==legacy.transfer.dsp_workspace_bytes&&
        normalized.transfer.dsp_workspace_bytes==normalized.dsp_workspace_bytes&&
        capacity==live::detail::bank_capacity(normalized),
        "pure capacity and actual shared normalization/reservation must use identical bytes");
    explicit_settings.simulation=true;
    check(live::receiver_workspace_capacity(explicit_settings)>capacity,
        "simulation must retain replay charges while removing hardware capture/audio reservations");
    explicit_settings=legacy;explicit_settings.exclusive=true;
    check(live::receiver_workspace_capacity(explicit_settings)==capacity,
        "pure allocation geometry cannot replace provider-selected exclusive support with a platform guess");
    audio::Options options;options.exclusive=true;audio::validate_option_values(options);
    auto invalid=legacy;invalid.transmit_gain=std::numeric_limits<double>::quiet_NaN();bool rejected=false;
    try {(void)live::receiver_workspace_capacity(invalid);}catch(const Error&){rejected=true;}
    check(rejected,"provider-free value normalization must preserve invalid-gain rejection");
    invalid=legacy;invalid.long_message_modem=invalid.transfer.modem;++invalid.long_message_modem->sample_rate;
    rejected=false;try {(void)live::receiver_workspace_capacity(invalid);}catch(const Error&){rejected=true;}
    check(rejected,"long/short physical profile mismatch cannot borrow pure allocation capacity");
    auto short_config=legacy.transfer.modem,long_config=short_config;long_config.integration_seconds=60;
    const auto exact=sizeof(live::detail::ReceiverState);
    check(live::receiver_control_margin(short_config,false)==65536U+exact&&
        live::receiver_control_margin(short_config,true)==65536U+exact&&
        live::receiver_control_margin(long_config,false)==65536U+exact&&
        live::receiver_control_margin(long_config,true)==8192U+exact,
        "control margins must charge the same concrete type used by Session, not a guessed sizeof");
    check(live::receiver_control_margin(short_config,true)-live::receiver_control_margin(long_config,true)==57344,
        "keyed long-symbol control-margin boundary changed");
}
void policy_and_epoch_normalization() {
    namespace policy=modem::detail::scheduled_acquisition_policy;
    const auto* old=std::getenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    const std::optional<std::string> saved=old?std::optional<std::string>{old}:std::nullopt;
    const auto put=[](const char* value) {
#ifdef _WIN32
        if(_putenv_s("DATAPUMP_SCHED_LAZY_ACQUISITION",value?value:"")!=0)throw Error("test policy environment failed");
#else
        if(value?setenv("DATAPUMP_SCHED_LAZY_ACQUISITION",value,1):unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION"))throw Error("test policy environment failed");
#endif
    };
    struct Restore {const std::optional<std::string>& saved;decltype(put)& restore_fn;
        ~Restore(){try{restore_fn(saved?saved->c_str():nullptr);}catch(...) {}}} restore{saved,put};
    for(const auto* value:{static_cast<const char*>(nullptr),"auto","1","0"}) {
        put(value);const bool enabled=!value||std::string_view(value)!="0";
        check(policy::requested()==enabled&&policy::select(1000,100)==enabled,"automatic/legacy policy disagreement");
        check(policy::select(10,100)==(value&&std::string_view(value)=="1"),
            "automatic must retain existing dense acceleration where its necessary state fits");
    }
    transfer::Options options;options.search_seconds=6;options.audio_timing_error_seconds=0;
    options.clock_sync=clock_sync::Policy{.01,10,0};
    check(live::detail::effective_epoch_search_seconds(options)==8,"combined propagation/GPS radius was not expanded");
    live::Settings settings;settings.transfer=options;
    check(live::detail::normalized_layout(settings).transfer.search_seconds==8,
        "estimator and Live automatic epoch normalization disagree");
    options.timestamp=1800000000;check(live::detail::effective_epoch_search_seconds(options)==6,
        "explicit epoch search radius must remain requested");
    options.clock_sync.reset();options.search_seconds=128;
    check(live::detail::effective_epoch_search_seconds(options,false)==128,
        "pure diagnostic estimator lost its wider bounded-search fallback");
    bool refused=false;try{(void)live::detail::effective_epoch_search_seconds(options);}catch(const Error&){refused=true;}
    check(refused,"actual Live must retain its explicit 60-second epoch limit");
}
void pure_estimator_consumer() {
    live::Settings settings;auto& options=settings.transfer;
    options.timestamp=1800000000;options.search_seconds=6;options.audio_timing_error_seconds=0;
    options.dsp_workspace_bytes=2609192632ULL;settings.dsp_workspace_bytes=options.dsp_workspace_bytes;
    options.key.emplace(Bytes(32,0x43));options.receive_targets_db_hz={8};options.automatic_receive_profiles=true;
    options.receive_pattern_mode=tuning::PatternMode::auto_keystream;
    options.modem=tuning::resolve(36,8,options.receive_pattern_mode,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    modem::OscillatorSearchConfig oscillator;
    oscillator.lf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));oscillator.rf=oscillator.lf;
    oscillator.rf_shift_hz=3e7;oscillator.margin=3;oscillator.reference=modem::OscillatorReference::independent_audio;
    options.modem.oscillator_search=oscillator;
    auto schedule=std::make_shared<modem::SchedHopPlan>();schedule->time_multiplier=10;schedule->audio_error_seconds=0;
    options.modem.scheduled_hopping=schedule;
    const auto draft=transfer::estimate_binary(Bytes{0},options);modem::ChannelConfig channel;
    channel.clock_error_ppm=0;channel.frequency_offset_hz=0;channel.phase_noise_degrees_per_sqrt_second=0;
    const auto estimate=simulation::estimate_with_receiver_settings(draft,options,true,channel,settings,{},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);
    check(estimate.profile_matches&&estimate.receiver_geometry.sample_rate==options.modem.sample_rate&&
        estimate.receiver_geometry.symbol_samples==modem::symbol_sample_count(options.modem),
        "standalone pure estimator must consume scheduled settings without changing source geometry");
    check(!estimate.gpu_work_supported&&!estimate.confidence_available,
        "provider-free planning cannot invent GPU execution or scheduled probability qualification");
}
}
int main(){try {structural_normalization_and_reservations();policy_and_epoch_normalization();pure_estimator_consumer();
    std::cout<<"provider-free Live allocation/planning link regression passed\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
