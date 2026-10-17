// This executable must link only datapump: no audio objects, policies, mocks,
// provider traps or stubs. Its successful link is the primary regression gate.
#include "datapump/live.hpp"
#include "datapump/runtime.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/simulation_estimate.hpp"
#include "../src/live_receiver_layout.hpp"
#include "../src/live_receiver_admission.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/streaming_modem.hpp"
#include "../src/scheduled_acquisition_policy.hpp"
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <array>
#include <random>
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
            "automatic must retain existing dense acceleration where its complete mandatory state fits");
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
void public_amplitude_clock_window() {
    constexpr auto epoch=1800000000ULL;
    live::Settings value;auto& c=value.transfer.modem;
    c.sample_rate=512;c.bandwidth_hz=4;c.carrier_hz=128;c.spreading_factor=64;
    c.pattern_symbols=true;c.constellation_bits=1;c.pulse_shaping=true;
    c.scramble=c.dsss=false;c.dsss_factor=1;c.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
    c.oscillator_search.emplace();c.oscillator_search->lf={0,0};c.oscillator_search->rf={0,0};
    c.oscillator_search->reference=modem::OscillatorReference::independent_audio;
    value.transfer.automatic_receive_profiles=false;value.transfer.timestamp=0;value.transfer.search_seconds=6;
    value.transfer.clock_sync=clock_sync::Policy{.001,.4,2.564};value.transfer.audio_timing_error_seconds=.2;
    value.dsp_workspace_bytes=value.transfer.dsp_workspace_bytes=256U*1024*1024;
    const audio::TimePrediction stamp{static_cast<long double>(epoch)-5,.02,1./c.sample_rate,audio::TimingQuality::estimated,0};
    const auto prepare=[&](const live::Settings& settings,std::optional<audio::TimePrediction> timing) {
        live::detail::ReceiverAdmissionContext context;context.timing=timing;context.now=double(epoch)-5;
        context.propagation=settings.transfer.clock_sync?settings.transfer.clock_sync->offset_seconds:0;
        context.keyed=false;context.capacity=live::receiver_workspace_capacity(settings);
        context.control_margin=live::receiver_control_margin(settings.transfer.modem,false);
        return live::detail::prepare_receiver_search(settings.transfer.modem,settings,epoch,context);
    };
    const auto prepared=prepare(value,stamp);
    check(!prepared.clock_window_modeled&&!prepared.search.qualified_start_window&&prepared.search.amplitude_start_window,
        "public amplitude timing must have its own prior without narrowing complex search");
    check(prepared.search.amplitude_lookahead_epochs>=12,
        "dated public amplitude lacks a future admission horizon");
    auto no_horizon=prepared.search;no_horizon.amplitude_lookahead_epochs=0;
    check(prepared.search.amplitude_receiver_bank_count==
          modem::receiver_strategy_amplitude_bank_count(c,no_horizon,1,1)+prepared.search.amplitude_lookahead_epochs,
        "future public amplitude cohorts were not charged before selection");
    auto slow_stamp=stamp;slow_stamp.seconds_per_frame*=2;
    check(prepare(value,slow_stamp).search.amplitude_lookahead_epochs>prepared.search.amplitude_lookahead_epochs,
        "future admission ignored the capture slope");
    check(!live::detail::amplitude_epoch_lookahead(c,prepared.search,
              std::numeric_limits<long double>::quiet_NaN(),0),"invalid capture slope invented a future horizon");
    const auto crossing=live::detail::amplitude_epoch_lookahead(c,prepared.search,.9L/c.sample_rate,.2L);
    const auto equivalent=live::detail::amplitude_epoch_lookahead(c,prepared.search,1.1L/c.sample_rate,0);
    check(crossing==equivalent,"future horizon used multiplicative rather than additive capture uncertainty");
    // ArrivalMap includes an explicit UTC arithmetic allowance in addition to
    // the physical GPS/region/audio radius; MSVC long double has double precision.
    const auto timestamp_roundoff=64*std::numeric_limits<long double>::epsilon()*
        std::max({1.L,std::abs(static_cast<long double>(epoch)+.1L),std::abs(stamp.utc_seconds)});
    const auto expected_half_width=.402L+.01L/c.sample_rate+timestamp_roundoff;
    const auto prior=*prepared.search.amplitude_start_window;
    check(std::abs(prior.epoch_origin_samples/c.sample_rate-7.664)<1e-6&&
          std::abs(prior.half_width_samples/c.sample_rate-expected_half_width)<1e-6,
          "public amplitude omitted GPS/total-region/offset/causal-audio bounds");
    const auto compare=[&](const live::Settings& settings,double center_delta,double radius_delta) {
        const auto other=prepare(settings,stamp).search.amplitude_start_window;
        check(other&&std::abs((other->epoch_origin_samples-prior.epoch_origin_samples)/c.sample_rate-center_delta)<1e-6&&
              std::abs((other->half_width_samples-prior.half_width_samples)/c.sample_rate-radius_delta)<1e-6,
              "amplitude timing controls changed the wrong center or half-width");
    };
    auto changed=value;changed.transfer.clock_sync->region_seconds=.1;compare(changed,0,-.15);
    changed=value;changed.transfer.clock_sync->accuracy_seconds=.005;compare(changed,0,.008);
    changed=value;changed.transfer.clock_sync->offset_seconds+=1;compare(changed,1,0);
    changed=value;changed.transfer.audio_timing_error_seconds=.1;compare(changed,-.05,-.1);
    changed=value;changed.transfer.clock_sync.reset();
    const auto default_search=prepare(changed,stamp).search;
    check(!default_search.amplitude_start_window&&!default_search.qualified_start_window&&
          default_search.amplitude_epoch_stride_seconds==64&&default_search.amplitude_start_uncertainty_seconds==33&&
          default_search.amplitude_lookahead_epochs>=34&&default_search.start_uncertainty_seconds==value.transfer.search_seconds+1.,
          "Default public windows lost continuous preadmission or narrowed the complex arrival domain");
    check(!prepare(value,{}).search.amplitude_start_window,"missing provider date invented an amplitude prior");
    auto uncertain=stamp;uncertain.uncertainty_seconds=.101;
    check(!prepare(value,uncertain).search.amplitude_start_window,"amplitude clamped provider uncertainty to Audio allowance");
    changed=value;changed.transfer.audio_timing_error_seconds=0;
    check(!prepare(changed,stamp).search.amplitude_start_window,"Audio0 qualified a positive-uncertainty provider");
    changed=value;changed.simulation=true;
    check(!prepare(changed,stamp).search.amplitude_start_window,"synthetic startup acquired a hardware timing prior");
    changed=value;changed.transfer.modem.oscillator_search.reset();
    check(prepare(changed,stamp).search.amplitude_start_window.has_value(),"clock arrival assumptions incorrectly required an oscillator preset");
    auto broad=prepared.search;broad.amplitude_start_window.reset();
    const auto full=modem::resolve_receiver_strategy(c,broad,8U*1024*1024);
    const auto narrow=modem::resolve_receiver_strategy(c,prepared.search,8U*1024*1024);
    check(narrow.supported&&narrow.backend==modem::ReceiverBackend::amplitude_energy&&
          narrow.amplitude_processing_ratio<full.amplitude_processing_ratio&&narrow.amplitude_channels==full.amplitude_channels,
          "amplitude prior did not reduce envelope work or changed channel geometry");
    auto complex=c;complex.receiver_strategy=modem::ReceiverStrategy::existing_only;
    const auto complex_full=modem::resolve_receiver_strategy(complex,broad,8U*1024*1024);
    const auto complex_prior=modem::resolve_receiver_strategy(complex,prepared.search,8U*1024*1024);
    check(complex_prior.backend==complex_full.backend&&complex_prior.existing_processing_ratio==complex_full.existing_processing_ratio&&
          complex_prior.selected_workspace_bytes==complex_full.selected_workspace_bytes,
          "amplitude-only timing prior changed continuous complex policy");
    // Count completed first-symbol candidates in the actual wrapper, proving
    // promotion happens only after amplitude selection and retains cell edges.
    modem::PatternSearch cells;cells.start_offset_seconds=7;cells.start_uncertainty_seconds=7;
    cells.hypotheses={{0,0}};cells.worker_threads=1;cells.retain_score=0;cells.candidate_limit=4096;
    const auto count=[&](std::optional<modem::PatternStartWindow> window) {
        auto search=cells;search.amplitude_start_window=window;
        modem::PatternReceiver receiver(c,8U*1024*1024,search);
        std::vector<float> zeros(50*c.sample_rate);receiver.push(zeros);
        const auto candidates=receiver.candidates();
        return std::count_if(candidates.begin(),candidates.end(),[](const auto& item){return item.stream_symbol==0;});
    };
    const auto all=count({});
    const auto inside=count(modem::PatternStartWindow{2.L*c.sample_rate,1,.00001L});
    const auto boundary=count(modem::PatternStartWindow{2.0625L*c.sample_rate,1,.00001L});
    check(all>inside&&inside==1&&boundary==2,"amplitude prior shifted timing cells or failed to prune actual receiver candidates");
    transfer::Estimate wire;wire.wire_bits=1;wire.total_seconds=modem::symbol_seconds(c);
    modem::ChannelConfig channel;channel.clock_error_ppm=channel.frequency_offset_hz=channel.phase_noise_degrees_per_sqrt_second=0;
    const auto estimate=simulation::estimate_with_receiver_settings(wire,value.transfer,true,channel,value,{},1,false,
        100,256,simulation::ReceiverWorkMode::hardware_timing_model,{.02,0,1./c.sample_rate});
    check(estimate.timing_window_modeled&&estimate.receiver_geometry.arrival_window_available&&
          std::abs(estimate.receiver_geometry.combined_arrival_half_width_seconds-expected_half_width)<1e-6,
          "planner did not report the selected amplitude timing model");
    std::cout<<"public amplitude timing: "<<all<<" broad cells, "<<inside<<" inside, "<<boundary<<" boundary cells\n";
    // Isolate the actual strong two-bit waveform from Live admission. The
    // near-edge old lane is diagnostic; exact and binary-refined lanes must
    // preserve both bits and physical completion with the unchanged statistic.
    auto edge=c;edge.bandwidth_hz=5;edge.stream_epoch=epoch;
    transfer::Options edge_options;edge_options.modem=edge;edge_options.timestamp=epoch;
    auto tx=transfer::binary_transmitter(Bytes{0,1},edge_options);
    constexpr std::size_t delay=12*512+37,input_origin=1377;
    const auto source_start=delay+modem::training_sample_count(edge)+modem::pattern_pulse_padding_samples(edge);
    std::vector<float> samples(delay+tx->total_samples()+2*modem::symbol_sample_count(edge));
    for(std::size_t at=delay;at<delay+tx->total_samples();)
        at+=tx->read(std::span(samples).subspan(at,delay+tx->total_samples()-at));
    std::mt19937 random(7831);std::normal_distribution<float> noise(0,.002F);
    for(auto& x:samples)x+=noise(random);
    for(const auto origin:{7774.L,static_cast<long double>(source_start),7825.25L}) {
        modem::PatternSearch fixed;fixed.input_sample_origin=input_origin;
        fixed.start_offset_seconds=static_cast<double>((origin-input_origin)/edge.sample_rate);
        fixed.start_uncertainty_seconds=0;fixed.search_stream_phases=false;fixed.hypotheses={{0,0}};
        fixed.worker_threads=1;fixed.retain_score=0;fixed.candidate_limit=2048;
        modem::PatternReceiver rx(edge,8U*1024*1024,fixed);
        std::string bits;bool complete=false;std::array<double,2> scores{},thresholds{};
        for(std::size_t at=input_origin;at<samples.size();) {
            const auto count=std::min<std::size_t>(51,samples.size()-at);
            rx.push(std::span(samples).subspan(at,count));at+=count;
            for(const auto& burst:rx.take_bursts()) {
                for(auto bit:burst.bits)bits.push_back(bit?'1':'0');
                complete|=burst.complete;
            }
            for(const auto& evidence:rx.candidates())if(evidence.stream_symbol<2) {
                scores[evidence.stream_symbol]=evidence.score;
                thresholds[evidence.stream_symbol]=evidence.admission_threshold;
            }
        }
        std::cout<<"amplitude cell origin="<<origin<<" true="<<source_start<<" bits="<<bits
                 <<" scores="<<scores[0]<<','<<scores[1]<<" thresholds="<<thresholds[0]<<','<<thresholds[1]<<'\n';
        if(origin!=7774.L)check(bits=="01"&&complete,"exact/refined amplitude lane lost two-bit/END control");
    }
    modem::PatternSearch competing;competing.input_sample_origin=input_origin;
    competing.start_offset_seconds=12.485937465667725;competing.start_uncertainty_seconds=7;
    competing.amplitude_start_window=modem::PatternStartWindow{6444,1,205.8340064L};
    competing.hypotheses={{0,0}};competing.worker_threads=1;
    {
        modem::PatternReceiver partial(edge,8U*1024*1024,competing);
        partial.push(std::span(samples).subspan(input_origin,20992-input_origin));
        check(partial.take_bursts().empty(),"dated amplitude selected its initial owner before competing starts were scored");
        partial.finish();std::string bits;bool complete=false;
        const auto bursts=partial.take_bursts();
        for(const auto& burst:bursts) {
            check(!burst.missing_slots&&burst.stream_first_symbol==0&&burst.first_stream_symbol==0,
                "EOF pending prefix changed ordinal or introduced missing slots");
            for(auto bit:burst.bits)bits.push_back(bit?'1':'0');complete|=burst.complete;
        }
        check(bursts.size()==1&&bits=="0"&&!complete,"EOF dropped the best available pending prefix or manufactured END");
    }
    auto one=transfer::binary_transmitter(Bytes{0},edge_options);
    std::vector<float> single(delay+one->total_samples()+2*modem::symbol_sample_count(edge));
    for(std::size_t at=delay;at<delay+one->total_samples();)
        at+=one->read(std::span(single).subspan(at,delay+one->total_samples()-at));
    for(auto& x:single)x+=noise(random);
    competing.bit_limit=1;
    modem::PatternReceiver bounded(edge,8U*1024*1024,competing);
    bounded.push(std::span(single).subspan(input_origin));
    std::string bounded_bits;bool bounded_complete=false;
    const auto bursts=bounded.take_bursts();
    std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
    std::size_t terminals=0,nonempty=0;
    for(const auto& burst:bursts) {
        const auto current=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
        if(!identity)identity=current;
        check(*identity==current&&!burst.missing_slots&&burst.stream_first_symbol==0&&
              burst.first_stream_symbol==bounded_bits.size(),
              "one-bit large push changed physical identity, ordinal or missing slots");
        if(burst.complete){++terminals;check(burst.bits.empty(),"terminal event repeated accepted data");}
        if(!burst.bits.empty()){++nonempty;check(!burst.complete,"one-bit pending event acquired early END");}
        for(auto bit:burst.bits)bounded_bits.push_back(bit?'1':'0');bounded_complete|=burst.complete;
    }
    check(bursts.size()==2&&terminals==1&&nonempty==1&&bounded_bits=="0"&&bounded_complete,"dated initial ownership exhausted the one-bit budget during a large push");
}
void public_compact_workspace_admission() {
    transfer::Options options;options.timestamp=1800000000;options.search_seconds=6;
    options.dsp_workspace_bytes=32U*1024*1024;
    auto& c=options.modem;c.sample_rate=131072;c.bandwidth_hz=32768;c.carrier_hz=32768;
    c.integration_seconds=8;c.spreading_factor=1;c.pattern_symbols=true;c.constellation_bits=1;
    c.receiver_strategy=modem::ReceiverStrategy::existing_only;
    live::Settings settings;settings.transfer=options;settings.dsp_workspace_bytes=options.dsp_workspace_bytes;
    settings.simulation=false;
    check(live::receiver_workspace_capacity(settings)>0,"public compact fixture exhausted frontend reserves");
    // Even the larger half-budget cannot hold the original complete timing
    // bank. Exercise the real constructor without generating any PCM.
    modem::PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=7;
    search.expand_clock_search=true;search.allow_local_clock_fallback=true;
    bool rejected=false;
    try {modem::PatternReceiver receiver(c,options.dsp_workspace_bytes/2,search);}
    catch(const Error& error){rejected=std::string_view(error.what()).find("workspace")!=std::string_view::npos;}
    check(rejected,"public compact fixture unexpectedly fits the real receiver");
    const auto wire=transfer::estimate_binary(Bytes{0},options);
    const auto estimate=simulation::estimate(wire,options,true,modem::ChannelConfig{}, {},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);
    check(!estimate.receiver_workspace_supported,
          "public compact estimate bypassed its memory check without an oscillator model");
}
void ordinary_settings_follow_live_admission() {
    transfer::Options options;options.timestamp=1800000000;options.search_seconds=6;
    options.key.emplace(Bytes(32,0x63));
    auto& c=options.modem;c.sample_rate=512;c.bandwidth_hz=4;c.carrier_hz=128;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=512;c.scramble=true;
    c.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
    std::optional<live::Settings> first_fit;
    std::optional<modem::ReceiverStrategyPlan> expected;
    bool rejected=false;
    for(std::size_t mib=16;mib<=2048;mib+=16) {
        options.dsp_workspace_bytes=mib*1024*1024;
        live::Settings settings;settings.transfer=options;settings.dsp_workspace_bytes=options.dsp_workspace_bytes;
        std::size_t capacity=0;
        try{capacity=live::receiver_workspace_capacity(settings);}catch(const Error&){rejected=true;continue;}
        const auto config=transfer::seeded_config(options,options.timestamp);
        const auto control=live::receiver_control_margin(config,true);
        live::detail::ReceiverAdmissionContext admission;
        admission.now=options.timestamp;admission.capacity=capacity;admission.control_margin=control;
        admission.keyed=true;admission.compact_clock_search=true;admission.prefer_streamed_templates=true;
        const auto prepared=live::detail::prepare_receiver_search(config,settings,options.timestamp,admission);
        const auto plan=modem::resolve_receiver_strategy(config,prepared.search,
            std::min(options.dsp_workspace_bytes/2,capacity>control?capacity-control:0));
        if(!plan.supported){rejected=true;continue;}
        check(plan.backend==modem::ReceiverBackend::amplitude_energy,"boundary fixture selected another decoder");
        first_fit=settings;expected=plan;break;
    }
    check(rejected&&first_fit&&expected,"ordinary amplitude workspace boundary was not exercised");
    const auto& selected=first_fit->transfer;
    transfer::Estimate wire;wire.wire_bits=2;wire.total_seconds=2*modem::symbol_seconds(selected.modem);
    modem::ChannelConfig channel;channel.clock_error_ppm=0;channel.frequency_offset_hz=0;
    channel.phase_noise_degrees_per_sqrt_second=0;
    const auto result=simulation::estimate_with_receiver_settings(wire,selected,true,channel,*first_fit,{},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);
    check(result.receiver_workspace_supported&&result.receiver_work_supported&&result.hardware_sustained_modeled,
          "ordinary estimate disagrees with actual Live amplitude admission");
    check(result.receiver_geometry.receiver_strategy_selected==modem::receiver_backend_name(expected->backend),
          "ordinary estimate selected a different decoder from Live");
    check(std::abs(result.hardware_sustained_realtime_ratio-expected->selected_processing_ratio)<
          1e-8*std::max(1.,expected->selected_processing_ratio),"ordinary estimate ignored Live workspace allowance");
    const auto inferred=simulation::estimate(wire,selected,true,channel,{},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);
    check(inferred.receiver_workspace_supported&&inferred.receiver_work_supported&&
          inferred.receiver_geometry.receiver_strategy_selected==result.receiver_geometry.receiver_strategy_selected&&
          std::abs(inferred.hardware_sustained_realtime_ratio-result.hardware_sustained_realtime_ratio)<1e-10,
          "standalone estimate retained an obsolete workspace basis");
    auto diagnostic=selected;diagnostic.search_seconds=128;diagnostic.dsp_workspace_bytes=1024;
    diagnostic.key.reset();
    const auto small=simulation::estimate(wire,diagnostic,true,channel,{},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);
    check(!small.receiver_workspace_supported,"tiny analytical budget invented receiver capacity");
    diagnostic.modem.receiver_strategy=modem::ReceiverStrategy::existing_only;
    diagnostic.modem.scramble=false;diagnostic.modem.oscillator_search.reset();
    const auto small_public=simulation::estimate(wire,diagnostic,true,channel,{},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);
    check(!small_public.receiver_workspace_supported,
          "public analytical estimate ignored exhausted shared workspace");
    auto invalid=*first_fit;invalid.transfer.modem.carrier_hz+=1;
    bool mismatch=false;try{(void)simulation::estimate_with_receiver_settings(wire,selected,true,channel,invalid,{},1,false,
        100,128,simulation::ReceiverWorkMode::hardware_fallback);}catch(const Error&){mismatch=true;}
    check(mismatch,"ordinary settings overload accepted a contradictory physical route");
}
void ordinary_legacy_workspace_admission() {
    for(const auto mib:{2U,3U}) {
        live::Settings settings;auto& options=settings.transfer;
        options.timestamp=1800000000;options.search_seconds=0;
        options.dsp_workspace_bytes=settings.dsp_workspace_bytes=mib*1024U*1024U;
        auto& c=options.modem;c.sample_rate=4096;c.bandwidth_hz=2048;c.carrier_hz=1024;
        c.integration_seconds=1;c.pattern_symbols=true;c.constellation_bits=1;
        c.receiver_strategy=modem::ReceiverStrategy::existing_only;
        const auto capacity=live::receiver_workspace_capacity(settings);
        const auto control=live::receiver_control_margin(c,false);
        check(capacity>control,"legacy admission fixture exhausted all shared capacity");
        const auto config=transfer::seeded_config(options,options.timestamp);
        live::detail::ReceiverAdmissionContext admission;
        admission.now=options.timestamp;admission.capacity=capacity;admission.control_margin=control;
        admission.key_count=admission.profile_count=1;
        const auto prepared=live::detail::prepare_receiver_search(config,settings,options.timestamp,admission);
        const auto allocation=std::min(options.dsp_workspace_bytes/2,capacity-control);
        bool fits=false;
        try {modem::StreamingReceiver receiver(config,allocation,prepared.search);fits=true;}
        catch(const Error& error) {
            check(std::string_view(error.what()).find("workspace")!=std::string_view::npos,
                  "legacy admission fixture failed for a reason other than workspace");
        }
        check(fits==(mib==3),"legacy admission fixture lost its real constructor boundary");
        transfer::Estimate wire;wire.wire_bits=1;wire.total_seconds=1;
        modem::ChannelConfig channel;channel.clock_error_ppm=channel.frequency_offset_hz=
            channel.phase_noise_degrees_per_sqrt_second=0;
        const auto estimate=simulation::estimate_with_receiver_settings(wire,options,true,channel,settings,{},1,false,
            100,128,simulation::ReceiverWorkMode::hardware_fallback);
        check(estimate.receiver_workspace_supported==fits,
              "legacy estimate spent memory already reserved for shared frontend/control state");
        check(estimate.receiver_geometry.per_bank_workspace_bytes<=allocation,
              "legacy estimate exceeded the real empty-bank constructor allocation");
    }
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
int main(){try {structural_normalization_and_reservations();policy_and_epoch_normalization();public_amplitude_clock_window();public_compact_workspace_admission();ordinary_settings_follow_live_admission();ordinary_legacy_workspace_admission();pure_estimator_consumer();
    std::cout<<"provider-free Live allocation/planning link regression passed\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
