#include "datapump/channel.hpp"
#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include "datapump/tuning.hpp"
#include "../src/live_helper.hpp"
#include "../src/live_signal_queue.hpp"
#include "../src/gui/state.hpp"
#include "../src/gui/record_presentations.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>
#include <map>
#include <memory>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string_view>
#include <tuple>
using namespace datapump;
namespace {
void check(bool value,const char* what) {if(!value)throw Error(what);}
modem::Config config() {
    modem::Config c;c.sample_rate=8000;c.bandwidth_hz=1000;c.carrier_hz=1500;
    c.spreading_factor=16;c.stream_epoch=1800000000;return c;
}
void punctuation_requires_consumed_evidence() {
    helper::Reference reference;reference.sample_rate=1000;reference.sample=1000;
    reference.timing_valid=true;reference.dwell_start_seconds=.75;reference.dwell_uncertainty_seconds=.001;
    // The anchor is at receiver sample 650, but the measured punctuation is
    // proved only at 900. An already captured later reference is frequency-only
    // until this particular receiver consumes that evidence too.
    auto timing=live::detail::helper_timing_evidence(reference,3,100,800,1000);
    check(!timing.prior,"a past dwell anchor admitted future punctuation evidence");
    timing=live::detail::helper_timing_evidence(reference,3,100,900,1000);
    // Neither phase nor metadata is qualified in this reference.
    check(timing.prior && timing.evidence_sample==900 && timing.prior->observed_sample==650 &&
          timing.prior->source_phase_samples==-1000 && timing.prior->period_samples==400,
          "causal punctuation lost its original periodic/epoch coordinates");
    reference.dwell_start_seconds=1.05;
    check(!live::detail::helper_timing_evidence(reference,3,100,1000,1000).prior,
          "punctuation anchor after its own evidence date was admitted");
    reference.dwell_start_seconds=.75;
    reference.sample=99;
    check(!live::detail::helper_timing_evidence(reference,3,100,900,1000).prior,
          "punctuation evidence before the receiver origin wrapped its date");
}
void helper_dates_survive_capture_clear() {
    helper::Reference reference;
    reference.sample_rate=8000;reference.sample=6400;reference.frequency_sample=5600;
    reference.dwell_start_seconds=.8;reference.frequency_valid=true;
    auto rebased=live::detail::rebase_helper_reference(reference,4000,8000,8000);
    check(rebased && rebased->sample==2400 && rebased->frequency_sample==1600 &&
          std::abs(rebased->dwell_start_seconds-.3)<1e-12,
          "capture clear left helper frequency and phase dates on different clocks");
    check(reference.sample==6400 && reference.frequency_sample==5600,
          "rebasing changed the capture observer's reference");
    reference.frequency_sample=0;
    rebased=live::detail::rebase_helper_reference(reference,4000,8000,8000);
    check(rebased && rebased->sample==2400 && rebased->frequency_sample==0,
          "capture clear changed the same-as-phase frequency sentinel");
    for(const auto stale:{3999ULL,4000ULL,6401ULL}) {
        reference.frequency_sample=stale;
        check(!live::detail::rebase_helper_reference(reference,4000,8000,8000),
              "capture clear accepted a stale or future independently dated frequency");
    }
    reference.frequency_sample=4001;
    rebased=live::detail::rebase_helper_reference(reference,4000,8000,8000);
    check(rebased && rebased->frequency_sample==1,
          "capture clear dropped the first valid independently dated frequency");
    reference.sample=8001;
    check(!live::detail::rebase_helper_reference(reference,4000,8000,8000),
          "helper phase date escaped the observed capture frontier");
    reference.sample=3999;reference.frequency_sample=0;
    check(!live::detail::rebase_helper_reference(reference,4000,8000,8000),
          "a pre-clear helper reference entered a replacement bank");
    reference.sample=6400;
    check(!live::detail::rebase_helper_reference(reference,4000,8000,6000),
          "helper reference entered a different sample clock");
}
void helper_frequency_support_survives_capture_clear() {
    for(const auto fit:{helper::FrequencyFit::local_polynomial,helper::FrequencyFit::conditioned_polynomial,
                        helper::FrequencyFit::masked_tone_profile}) {
        helper::Reference reference;
        reference.sample_rate=8000;reference.sample=6400;reference.frequency_sample=5600;
        reference.frequency_first_sample=4400;reference.frequency_fit=fit;reference.frequency_valid=true;
        reference.frequency_hz=2300.25;reference.frequency_uncertainty_hz=.125;
        reference.frequency_phase_diffusion_gain=.03125;reference.frequency_process_uncertainty_hz=.0625;
        reference.dwell_start_seconds=.8;
        auto rebased=live::detail::rebase_helper_reference(reference,4000,8000,8000);
        check(rebased && rebased->sample==2400 && rebased->frequency_first_sample==400 &&
              rebased->frequency_sample==1600 && rebased->frequency_sample+1==1601 &&
              rebased->frequency_first_sample<=rebased->frequency_sample &&
              rebased->frequency_sample<=rebased->sample,
              "capture clear did not translate the full causal helper frequency support");
        check(rebased->frequency_fit==fit && rebased->frequency_valid &&
              rebased->frequency_hz==reference.frequency_hz &&
              rebased->frequency_uncertainty_hz==reference.frequency_uncertainty_hz &&
              rebased->frequency_phase_diffusion_gain==reference.frequency_phase_diffusion_gain &&
              rebased->frequency_process_uncertainty_hz==reference.frequency_process_uncertainty_hz,
              "capture clear changed the helper fit or counted its included process uncertainty again");
        check(reference.sample==6400 && reference.frequency_first_sample==4400 &&
              reference.frequency_sample==5600 && reference.frequency_fit==fit,
              "rebasing mutated the observer's original helper frequency provenance");

        auto boundary=reference;boundary.frequency_first_sample=4000;
        rebased=live::detail::rebase_helper_reference(boundary,4000,8000,8000);
        check(rebased && rebased->frequency_first_sample==0 && rebased->frequency_sample==1600,
              "capture clear discarded frequency support beginning exactly at the new origin");
        auto straddling=reference;straddling.frequency_first_sample=3999;
        check(!live::detail::rebase_helper_reference(straddling,4000,8000,8000),
              "capture clear retained an optional frequency fit using pre-clear samples");
        auto reversed=reference;reversed.frequency_first_sample=5601;
        check(!live::detail::rebase_helper_reference(reversed,4000,8000,8000),
              "capture clear accepted helper frequency support starting after its endpoint");
        auto future=reference;future.frequency_sample=6401;
        check(!live::detail::rebase_helper_reference(future,4000,8000,8000),
              "capture clear accepted frequency support beyond the published observation");

        auto same_date=reference;same_date.frequency_sample=0;
        rebased=live::detail::rebase_helper_reference(same_date,4000,8000,8000);
        check(rebased && rebased->frequency_sample==0 && rebased->sample==2400 &&
              rebased->frequency_first_sample==400 && rebased->sample+1==2401 &&
              rebased->frequency_fit==fit &&
              rebased->frequency_uncertainty_hz==reference.frequency_uncertainty_hz &&
              rebased->frequency_phase_diffusion_gain==reference.frequency_phase_diffusion_gain &&
              rebased->frequency_process_uncertainty_hz==reference.frequency_process_uncertainty_hz,
              "capture clear changed the same-date helper frequency support or its uncertainty");
        same_date.frequency_first_sample=3999;
        check(!live::detail::rebase_helper_reference(same_date,4000,8000,8000),
              "same-date frequency sentinel concealed a fit straddling capture clear");
        // An independently dated endpoint at the new origin cannot become
        // zero: that value would instead mean the later phase/sample date.
        boundary.frequency_sample=4000;
        check(!live::detail::rebase_helper_reference(boundary,4000,8000,8000),
              "capture clear confused an origin endpoint with the same-date frequency sentinel");
    }
}
void helper_preserves_receiver_workspace_capacity() {
    for(const bool simulation:{false,true}) {
        live::Settings settings;settings.simulation=simulation;settings.transfer.modem=config();
        settings.dsp_workspace_bytes=32*1024*1024;settings.content_limit=1024*1024;
        settings.device="workspace planning must not open audio devices";
        settings.helper.power_w=0;
        const auto ordinary=live::receiver_workspace_capacity(settings);
        check(ordinary>0,"ordinary receiver workspace was not available");
        settings.helper.power_w=100;
        const auto assisted=live::receiver_workspace_capacity(settings);
        check(assisted==ordinary,simulation?
            "helper reduced simulated Robust receiver workspace capacity":
            "helper reduced native Robust receiver workspace capacity");
    }
}
void helper_placement_covers_both_profiles() {
    live::Settings settings;settings.transfer.modem=config();settings.transfer.modem.bandwidth_hz=100;
    auto longer=settings.transfer.modem;longer.dsss_factor=10;longer.scramble=true;settings.long_message_modem=longer;
    const auto width=live::detail::helper_occupied_bandwidth(settings);
    check(width>=helper::occupied_bandwidth(longer),
        "common helper placement omitted long-message occupied band");
    helper::Options options;options.power_w=100;
    modem::ChannelConfig impairment;impairment.clock_error_ppm=0;impairment.phase_noise_degrees_per_sqrt_second=0;
    modem::SampledSimulationChannel channel(settings.transfer.modem,impairment,options,width);
    const auto plan=helper::plan(options,settings.transfer.modem.carrier_hz,width,channel.physical_sample_rate(),
        0,modem::outer_dsss_transmit_gain(settings.transfer.modem));
    const auto data_upper=longer.carrier_hz+width/2;
    check(plan.auxiliary_frequency_hz>0 && plan.auxiliary_frequency_hz<plan.frequency_hz &&
          plan.auxiliary_frequency_hz-150>=data_upper+300,
        "closest auxiliary modulation band lost its long-message DATA guard");
    check(plan.frequency_hz-plan.auxiliary_frequency_hz==helper::auxiliary_pilot_spacing_hz &&
          plan.frequency_hz-(plan.auxiliary_frequency_hz+150)>=250,
        "pure pilot lost its separate auxiliary modulation guard");
    check(plan.frequency_hz+250<=channel.physical_sample_rate()/2.,
        "highest pure pilot lost its physical capture-edge margin");
    modem::StreamingTransmitter source(modem::RawBits{{0,0,1}},longer);
    std::array<float,800> samples{};helper::Tracker tracker(plan.frequency_hz);
    bool observed=false;
    channel.set_physical_observer([&](std::span<const float> pcm,std::uint32_t rate,std::uint64_t) {
        tracker.push(pcm,rate);if(const auto update=tracker.take_update())observed=observed||update->present;
    });
    for(unsigned i=0;i<20;++i) {
        if(!channel.read(source,samples))break;
    }
    check(observed,"long-message waveform and common helper did not share the sampled channel");
}
void sampled_composite() {
    const auto c=config();modem::ChannelConfig impairment;
    impairment.snr_db=20;impairment.clock_error_ppm=0;impairment.frequency_offset_hz=2;
    impairment.phase_noise_degrees_per_sqrt_second=0;impairment.seed=581;
    helper::Options options;options.power_w=1500;options.data_power_w=4;
    modem::SampledSimulationChannel baseline(c,impairment),disabled(c,impairment,{}),assisted(c,impairment,options);
    const auto plan=helper::plan(options,c.carrier_hz,
        helper::occupied_bandwidth(c),assisted.physical_sample_rate(),0,
        modem::outer_dsss_transmit_gain(c));
    std::array<float,800> a{},b{},h{};
    baseline.read_noise(a);disabled.read_noise(b);assisted.read_noise(h);
    check(a==b,"disabled helper changed original simulation noise");
    check(assisted.physical_sample_rate()>2*c.sample_rate,"helper reused the unisolated logical sample clock");
    baseline.begin_burst();disabled.begin_burst();assisted.begin_burst();
    modem::StreamingTransmitter source_a(modem::RawBits{{0,0,1}},c),source_b(modem::RawBits{{0,0,1}},c);
    const auto n=baseline.read(source_a,a),m=disabled.read(source_b,b);
    check(n==m && a==b,"disabled helper changed sampled payload bits or waveform");
    modem::StreamingTransmitter noise(modem::Noise{},c);
    helper::Tracker tracker(plan.frequency_hz,20);
    bool present=false,confirmed=false;
    assisted.set_physical_observer([&](std::span<const float> pcm,std::uint32_t rate,std::uint64_t) {
        tracker.push(pcm,rate);
        if(const auto observation=tracker.take_update()) {
            present=present||observation->present;confirmed=confirmed||observation->confirmed;
            if(observation->present)check(std::abs(observation->frequency_hz-(plan.frequency_hz+2))<4,
                "composite helper frequency was not observed through actual sampled channel");
        }
    });
    for(unsigned block=0;block<75;++block) {
        const auto count=assisted.read(noise,h);check(count==h.size(),"helper noise source ended unexpectedly");
    }
    check(present && confirmed,"sampled helper composite did not sustain six seconds of independent presence");
}
void helper_only_and_callsign_update() {
    auto c=config();modem::ChannelConfig impairment;impairment.snr_db=20;
    impairment.clock_error_ppm=0;impairment.phase_noise_degrees_per_sqrt_second=0;
    helper::Options options;options.power_w=.04;
    modem::SampledSimulationChannel channel(c,impairment,options);
    channel.set_helper_callsign("N0CALL");channel.begin_burst(true);
    modem::StreamingTransmitter source(modem::Noise{},c);std::array<float,800> samples{};
    double energy=0;std::size_t count=0;
    for(unsigned i=0;i<20;++i) {
        count+=channel.read(source,samples);
        for(float value:samples)energy+=value*value;
    }
    check(energy/count<.01,"helper-only channel leaked ordinary Robust payload power");
    modem::SampledSimulationChannel disabled(c,impairment);
    bool rejected=false;try {disabled.begin_burst(true);}catch(const Error&) {rejected=true;}
    check(rejected,"disabled helper-only channel transmitted ordinary payload instead");
}
void callsign_update_preserves_live_reception() {
    live::Settings settings;settings.simulation=true;settings.transfer.modem=config();
    settings.transfer.timestamp=1800000000;settings.transfer.search_seconds=0;
    settings.simulation_clock_error_ppm=0;settings.simulation_phase_noise_degrees_per_sqrt_second=0;
    settings.dsp_workspace_bytes=32*1024*1024;settings.content_limit=1024*1024;settings.helper.power_w=100;
    live::Session session;session.start(settings);session.transmit_noise();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    live::Snapshot before;
    do {
        before=session.snapshot();
        check(before.error.empty(),"live noise setup failed before helper callsign edit");
        if(before.transmitting_noise && before.transmission_seconds>0)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while(std::chrono::steady_clock::now()<deadline);
    check(before.transmitting_noise&&before.transmission_seconds>0,"live noise did not begin");
    check(before.helper_status.find("distinct physical audio rate")==std::string::npos,
        "physical helper simulation retained its obsolete unavailability fallback");
    session.set_helper_callsign("N0CALL");
    const auto after=session.snapshot();
    check(after.transmitting_noise&&!after.transmission_cancelled && after.transmission_id==before.transmission_id &&
        after.receiver_input_revision==before.receiver_input_revision,
        "helper callsign edit interrupted active transmission or replaced the pending receiver");
    session.cancel_transmit();session.stop();
}
void helper_only_live_simulation(bool wide_offset=false) {
    live::Settings settings;settings.simulation=true;settings.full_duplex=true;settings.transfer.modem=config();
    settings.transfer.timestamp=1800000000;settings.transfer.search_seconds=0;settings.simulation_snr_db=100;
    settings.simulation_clock_error_ppm=0;settings.simulation_phase_noise_degrees_per_sqrt_second=0;
    settings.dsp_workspace_bytes=32*1024*1024;settings.content_limit=1024*1024;
    settings.helper.power_w=1500;settings.helper.callsign="N0CALL";
    if(wide_offset) {
        const auto crystal=tuning::oscillator_model(tuning::parse_oscillator_preset("crystal"));
        settings.transfer.modem.oscillator_search=modem::OscillatorSearchConfig{
            crystal,crystal,3e7,3,modem::OscillatorReference::shared_radio};
        settings.transfer.timestamp=0;
        settings.simulation_frequency_offset_hz=3000;settings.simulation_clock_error_ppm=100;
        check(settings.transfer.modem.receiver_strategy==modem::ReceiverStrategy::automatic &&
              crystal.accuracy_ppm==100,"wide auxiliary fixture changed its ordinary oscillator/Auto policy");
    }
    live::Session session;session.start(settings);session.transmit_helper();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    bool helper_row=false,callsign=false,observed_wide_offset=false;std::uint64_t helper_id=0;
    do {
        const auto snapshot=session.snapshot();check(snapshot.error.empty(),"helper-only Live simulation failed");
        if(wide_offset) {
            check(!snapshot.receiver_health.failed() && !snapshot.receiver_health.dropped_samples &&
                  snapshot.dsp_buffered_bytes<=settings.dsp_workspace_bytes,
                  "wide auxiliary helper changed receiver health or exceeded its original workspace");
            observed_wide_offset=observed_wide_offset || (snapshot.helper_frequency_offset_hz &&
                std::abs(*snapshot.helper_frequency_offset_hz)>2500);
        }
        check(snapshot.received.empty(),"helper-only Live simulation manufactured received payload content");
        for(const auto& event:snapshot.signals) {
            check(event.helper_carrier||!event.received_bits,"helper-only Live simulation manufactured binary bits");
            if(!event.helper_carrier)continue;
            helper_row=true;if(!helper_id)helper_id=event.id;
            check(event.id==helper_id,"ongoing Live helper replaced its stable signal row");
            callsign=callsign||event.text.find("N0CALL")!=std::string::npos;
        }
        if(helper_row&&callsign&&snapshot.helper_frequency_offset_hz&&(!wide_offset||observed_wide_offset))break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(std::chrono::steady_clock::now()<deadline);
    check(helper_row,"helper-only physical simulation did not publish a received helper row");
    check(callsign,"strong coherent Live helper did not decode its callsign metadata");
    if(wide_offset)check(observed_wide_offset,"wide auxiliary fixture did not acquire its actual displaced pilot");
    session.clear_received();
    bool after_clear=false;
    const auto clear_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        const auto snapshot=session.snapshot();check(snapshot.error.empty(),"helper simulation failed after receive clear");
        for(const auto& event:snapshot.signals) {
            check(event.helper_carrier||!event.received_bits,"helper-only clear manufactured binary bits");
            after_clear=after_clear||(event.helper_carrier&&event.id!=helper_id);
        }
        if(after_clear)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(std::chrono::steady_clock::now()<clear_deadline);
    session.cancel_transmit();session.stop();
    check(after_clear,"continuous physical helper did not resume its receive row after clear");
}
bool lifecycle_trace_event(const char* name)noexcept {
    const std::string_view event{name};
    return event=="trace_begin" || event=="receiver_helper_observation_started" || event=="receiver_helper_raw_reference" ||
        event=="receiver_helper_corrected_interval" || event=="receiver_helper_reference_reentry" ||
        event=="receiver_acquisition_work" || event=="receiver_acquisition_work_unavailable" ||
        event=="receiver_bank_admitted" || event=="receiver_signal_commit" ||
        event=="receiver_helper_assistance_admitted" || event.starts_with("receiver_helper_cohort_") ||
        event.starts_with("receiver_helper_conditioning_") || event.starts_with("receiver_helper_future_broad_");
}
void helper_audit_work_credit_is_bounded() {
    live::detail::HelperAuditTokens native,compact;
    native.earn(7);check(!native.ready(),"optional audit used work that had not executed");
    native.earn(8);check(native.ready()&&!compact.ready(),"audit work units were pooled across receiver backends");
    native.charge(80);check(!native.ready()&&native.debt==79,"one indivisible audit packet escaped its debt");
    native.earn(640);check(!native.ready()&&!native.debt,"audit debt repayment minted another packet");
    native.earn(648);check(native.ready(),"actual conditional work did not replenish an audit credit");
    native.charge(1);native.earn(1000000);
    check(native.credit==1&&native.ready(),"old work accumulated an unbounded audit burst");
    native.charge(1);native.earn(1);
    check(!native.ready(),"a reset work counter minted optional audit credit");
}
void rolling_helper_waits_and_drains_after_loss() {
    live::Settings settings;settings.simulation=true;settings.full_duplex=true;
    auto& c=settings.transfer.modem;c=config();c.bandwidth_hz=200;c.spreading_factor=64;
    c.pulse_shaping=false;c.scramble=true;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{.01,0},{1,0},1e6,3};
    std::array<std::uint8_t,32> secret{};secret[0]=29;settings.transfer.key=Crypto(secret);
    settings.transfer.timestamp=0;settings.transfer.search_seconds=0;
    settings.simulation_snr_db=40;settings.simulation_clock_error_ppm=0;
    settings.simulation_frequency_offset_hz=1;settings.simulation_phase_noise_degrees_per_sqrt_second=0;
    settings.dsp_workspace_bytes=64*1024*1024;settings.content_limit=1024*1024;
    settings.helper.power_w=1500;settings.helper.data_power_w=4;
    const auto path=std::filesystem::temp_directory_path()/
        ("datapump-helper-lifecycle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    const auto verify=[&](bool ok,const char* what) {
        if(!ok)throw Error(std::string(what)+"; lifecycle evidence retained: "+path.string());
    };
    bool assisted=false,waited=false,withdrawn=false,loss_drained=false;
    std::uint64_t loss_origin=0,withdraw_capture=0,last_samples=0,last_processed=0;
    std::uint64_t input_revision=0,withdraw_admissions=0;
    std::size_t peak_workspace=0;
    std::shared_ptr<audio::trace::State> trace_state;
    {
        // This is one realtime ordinary bank with its normal keyed epoch rows.
        // Manual Helper loss pauses fresh acquisition; it does not authorize a
        // second broad bank or turn unprocessed input into observed absence.
        audio::trace::Scope trace(path,false,&lifecycle_trace_event);
        trace_state=audio::trace::active.load();
        verify(trace_state && trace_state->path==std::filesystem::absolute(path).string(),
               "Helper lifecycle did not retain its own trace writer");
        live::Session session;session.start(settings);session.transmit_helper();
        const auto observe=[&](const live::Snapshot& snapshot) {
            verify(snapshot.error.empty() && !snapshot.receiver_health.failed() &&
                   !snapshot.receiver_health.dropped_samples,"rolling Helper changed ordinary receiver health");
            verify(snapshot.received.empty(),"helper-only rolling wait decoded payload content");
            if(snapshot.samples_received<36ULL*c.sample_rate)
                verify(!snapshot.receiver_admissions && !snapshot.receiver_instances,
                       "ordinary bank was constructed before the earliest possible local observation boundary");
            for(const auto& event:snapshot.signals)
                verify(event.helper_carrier || (!event.received_bits && !event.complete),
                       "helper-only rolling wait invented Robust bits or physical END");
            verify(snapshot.dsp_buffered_bytes<=settings.dsp_workspace_bytes,
                   "single realtime Helper bank exceeded its funded DSP workspace");
            peak_workspace=std::max(peak_workspace,snapshot.dsp_buffered_bytes);
            verify(snapshot.samples_received>=last_samples && snapshot.processed_physical_samples>=last_processed &&
                   snapshot.processed_physical_samples<=snapshot.samples_received,
                   "Helper lifecycle changed capture or processed sample chronology");
            if(input_revision)verify(snapshot.receiver_input_revision==input_revision,
                                    "Helper loss replaced the receiver capture revision");
            else input_revision=snapshot.receiver_input_revision;
            last_samples=snapshot.samples_received;last_processed=snapshot.processed_physical_samples;
            verify(!snapshot.helper_audit_jobs && !snapshot.helper_input_view_count,
                   "manual realtime Helper started a companion broad/input-view audit bank");
            return snapshot.helper_frequency_assistance_active;
        };
        // Allow the full 36-second media interval even on constrained hosts.
        // This does not relax any measured-media or completion assertion.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(480);
        do {
            const auto snapshot=session.snapshot();assisted=observe(snapshot)||assisted;
            if(last_samples>=40ULL*c.sample_rate && assisted) {waited=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(std::chrono::steady_clock::now()<deadline);
        loss_origin=last_samples;
        session.cancel_transmit();
        const auto loss_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        do {
            const auto snapshot=session.snapshot();const auto active=observe(snapshot);
            if(!withdrawn && !active && snapshot.helper_status.starts_with("Helper reference lost:")) {
                withdrawn=true;withdraw_capture=last_samples;withdraw_admissions=snapshot.receiver_admissions;
            }
            if(withdrawn) {
                verify(!active,"final Helper loss regained frequency authority from noise");
                verify(snapshot.receiver_admissions==withdraw_admissions,
                       "final Helper loss admitted a fresh ordinary acquisition row");
                // The observed withdrawal belongs to this capture date. The
                // processed frontier can still precede the first unlocked
                // input interval; wait until it has crossed the dated loss
                // itself plus three seconds of ordinary input.
                loss_drained=last_processed>=withdraw_capture+3ULL*c.sample_rate;
            }
            if(loss_drained)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(std::chrono::steady_clock::now()<loss_deadline);
        session.stop();
    }
    // The finalization receipt is emitted on stderr, not a sidecar file. As in
    // the cold-start/continuation fixtures, inspect this writer after Scope
    // teardown: a footer alone cannot prove its final flush and close.
    {
        std::lock_guard lock(trace_state->mutex);
        verify(trace_state->done && !trace_state->file && !trace_state->io_failed &&
               !trace_state->dropped && !trace_state->size_limited,
               "Helper lifecycle trace did not finish its flush and close");
    }
    const auto number=[&](const std::string& record,const char* name) {
        const auto key="\""+std::string(name)+"\":";const auto field=record.find(key);
        verify(field!=std::string::npos,"lifecycle trace omitted a required executed-work field");
        return std::stoull(record.substr(field+key.size()));
    };
    using Owner=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
    std::map<Owner,std::array<std::uint64_t,4>> work;
    std::ifstream input(path);verify(bool(input),"Helper lifecycle evidence was not written");
    std::string line;
    bool raw_qualified=false,corrected_qualified=false,joined_work=false,post_loss_join=false,footer=false;
    std::optional<std::uint64_t> corrected_end,unlocked_first,observation_deadline;
    std::uint64_t unlocked_end=0,admissions=0,rows=0;
    while(std::getline(input,line)) {
        const auto event=[&](std::string_view name) {
            return line.find("\"event\":\""+std::string(name)+"\"")!=std::string::npos;
        };
        verify(!event("receiver_acquisition_work_unavailable"),"Helper lifecycle lost a joined numeric work snapshot");
        verify(line.find("\"event\":\"receiver_helper_future_broad_")==std::string::npos &&
               line.find("\"event\":\"receiver_helper_cohort_")==std::string::npos &&
               line.find("\"event\":\"receiver_helper_conditioning_")==std::string::npos &&
               !event("receiver_helper_assistance_admitted"),
               "manual realtime Helper launched a legacy companion receiver or broad probe");
        if(event("receiver_helper_observation_started")) {
            const auto detected=number(line,"detection_frontier"),raw_rate=number(line,"sample_rate");
            verify(!observation_deadline && raw_rate && number(line,"minimum_seconds")==36,
                   "causal Helper observation was restarted or omitted its fixed interval");
            // This fixture's simulation Bank starts at source sample zero.
            const auto numerator=(detected+36ULL*raw_rate)*c.sample_rate;
            observation_deadline=(numerator+raw_rate-1)/raw_rate;
        }
        if(event("receiver_helper_raw_reference") && number(line,"qualified")) {
            verify(number(line,"sample")<number(line,"captured_end") &&
                   number(line,"frequency_first_sample")<=number(line,"sample"),
                   "qualified raw Helper reference used future or reversed support");
            raw_qualified=true;
        }
        if(event("receiver_helper_corrected_interval")) {
            const auto first=number(line,"first_sample"),end=number(line,"end_sample");
            verify(end>first && (!corrected_end || first==*corrected_end),
                   "single corrected input skipped or replayed payload sample coordinates");
            corrected_end=end;
            if(number(line,"locked")) {
                verify(!unlocked_first,"final Helper loss relocked the ordinary input on noise");
                verify(number(line,"corrected_interval_valid"),"locked corrected input omitted its measured RF interval");
                corrected_qualified=true;
            } else if(corrected_qualified && first>=loss_origin) {
                if(!unlocked_first)unlocked_first=first;
                unlocked_end=end;
            }
        }
        if(event("receiver_bank_admitted")) {
            verify(observation_deadline && number(line,"sample")>=*observation_deadline,
                   "ordinary receiver constructed before 36 seconds after causal presence detection");
            verify(number(line,"revision")==input_revision,"ordinary admission escaped the single capture bank revision");
            verify(!unlocked_first,"new ordinary receiver admission followed the actual Helper-loss fence");
            verify(number(line,"workspace")<=settings.dsp_workspace_bytes,
                   "ordinary admission exceeded the single-bank workspace ceiling");
            verify(number(line,"admissions")>admissions,"ordinary admission count did not advance");
            admissions=number(line,"admissions");++rows;
        }
        if(event("receiver_acquisition_work")) {
            verify(number(line,"revision")==input_revision && number(line,"realtime_helper") &&
                   !number(line,"helper_input_view") && number(line,"numeric_owner") &&
                   number(line,"counter_snapshot_available") && number(line,"counter_snapshot_joined"),
                   "executed work did not belong to the joined single realtime bank");
            verify(!number(line,"future_broad_template_jobs") && !number(line,"future_broad_carrier_samples") &&
                   !number(line,"future_broad_lane_samples") && !number(line,"audit_jobs"),
                   "manual realtime Helper executed a parallel broad acquisition");
            const auto original=number(line,"helper_window_original_pairs"),selected=number(line,"helper_window_selected_pairs");
            verify(!number(line,"helper_window_active") || (selected && selected<=original),
                   "executed Helper mask expanded or emptied its original RF/clock catalogue");
            const Owner owner{number(line,"revision"),number(line,"instance_id"),number(line,"sample_origin")};
            const std::array<std::uint64_t,4> counts{number(line,"template_jobs"),number(line,"input_transforms"),
                                                   number(line,"fft_jobs"),number(line,"direct_jobs")};
            if(const auto prior=work.find(owner);prior!=work.end())
                for(std::size_t i=0;i<counts.size();++i)
                    verify(counts[i]>=prior->second[i],"joined ordinary work counters regressed within one owner");
            work[owner]=counts;
            joined_work=joined_work || (number(line,"realtime_locked") && counts[0]);
            if(unlocked_first && !number(line,"realtime_locked")) {
                verify(!number(line,"dispatch_permitted") && !number(line,"helper_window_active"),
                       "Helper loss left fresh native acquisition or its RF mask armed");
                post_loss_join=post_loss_join || number(line,"sample")>=*unlocked_first+3ULL*c.sample_rate;
            }
        }
        if(event("receiver_signal_commit"))
            verify(!number(line,"received_bits") && !number(line,"complete"),
                   "helper-only joined receiver published invented bits or physical END");
        if(event("trace_end")) {
            verify(!number(line,"dropped") && line.find("\"size_limited\":false")!=std::string::npos &&
                   line.find("\"prior_io_failed\":false")!=std::string::npos,
                   "lifecycle trace was dropped, truncated or failed to write");
            footer=true;
        }
    }
    // This Helper-only fixture proves waiting and dated PCM drain, not payload
    // completion. test_helper_live_assistance::realtime_gap_protection separately
    // uses the actual Session-derived gap context, withdraws it after an accepted
    // prefix and checks retained ordinal progress plus fully scored physical END.
    std::cout<<"rolling realtime Helper assisted="<<assisted<<" waited="<<waited
             <<" raw_qualified="<<raw_qualified<<" corrected_qualified="<<corrected_qualified
             <<" joined_work="<<joined_work<<" owners="<<work.size()<<" admitted_rows="<<rows
             <<" peak_workspace="<<peak_workspace<<" withdrawn="<<withdrawn<<" drained="<<loss_drained
             <<" post_loss_join="<<post_loss_join<<" loss_origin="<<loss_origin
             <<" last_samples="<<last_samples<<" last_processed="<<last_processed<<'\n';
    verify(assisted && waited && observation_deadline && raw_qualified && corrected_qualified && joined_work && !work.empty() && rows &&
           peak_workspace && withdrawn && loss_drained && unlocked_first &&
           unlocked_end>=*unlocked_first+3ULL*c.sample_rate && post_loss_join && footer,
           "single-bank Helper waiting/loss/drain coverage was incomplete");
    std::filesystem::remove(path);
}
std::vector<float> physical_convert(std::span<const float> input,std::uint32_t from,std::uint32_t to,
        std::function<float(float,long double,std::uint32_t)> transform={}) {
    audio::Resampler converter(from,to);if(transform)converter.set_output_transform(std::move(transform));
    std::vector<float> result;std::array<float,1031> output{};std::size_t at=0;
    while(!converter.finished()) {
        const auto count=std::min<std::size_t>(173,input.size()-at);
        const auto progress=converter.process(input.subspan(at,count),output,at+count==input.size());
        check(progress.consumed||progress.produced||converter.finished(),"physical helper converter stalled");
        at+=progress.consumed;result.insert(result.end(),output.begin(),output.begin()+static_cast<std::ptrdiff_t>(progress.produced));
    }
    return result;
}
void payload_survives_physical_helper_preselection() {
    struct Geometry {std::uint32_t logical_rate;double carrier_hz,bandwidth_hz;};
    constexpr std::uint32_t physical_rate=48000;
    const std::array geometries{Geometry{8000,1500,1000},Geometry{2000,500,200}};
    for(const auto& geometry:geometries) {
        auto c=config();c.spreading_factor=64;c.sample_rate=geometry.logical_rate;
        c.carrier_hz=geometry.carrier_hz;c.bandwidth_hz=geometry.bandwidth_hz;
        modem::ChannelConfig impairment;impairment.snr_db=20;impairment.clock_error_ppm=0;
        impairment.frequency_offset_hz=0;impairment.phase_noise_degrees_per_sqrt_second=0;impairment.seed=581;
        modem::StreamingTransmitter source(modem::RawBits{{0,0,1}},c);
        modem::SampledSimulationChannel channel(c,impairment);
        std::vector<float> logical;std::array<float,173> samples{};
        while(const auto count=channel.read(source,samples))logical.insert(logical.end(),samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(count));
        const auto eof=logical.size();
        // Preserve the original 8.65 seconds of fully sampled trailing noise at
        // each geometry, independent of the logical sample rate.
        auto trailing=static_cast<std::uint64_t>(c.sample_rate)*173/20;
        while(trailing) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,samples.size()));
            channel.read_noise(std::span(samples).first(count));
            logical.insert(logical.end(),samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(count));trailing-=count;
        }
        std::array<std::uint64_t,2> baseline_completion{};std::array<std::vector<std::uint64_t>,2> baseline_bit_polls;
        struct Case {double power;bool only,scaled;const char* callsign;double end_phase;bool pcm16=false;};
        const std::array cases{
            Case{0,false,false,"",-1},Case{1500,false,false,"",-1},Case{4000000000.,false,false,"",-1},
            Case{1500,true,false,"",-1},Case{4000000000.,true,false,"",-1},
            Case{1500,false,true,"",-1},Case{4000000000.,false,true,"",-1},
            Case{1500,false,false,"N0CALL/TEST1",-1},Case{4000000000.,false,false,"N0CALL/TEST1",-1},
            Case{4000000000.,false,false,"N0CALL/TEST1",.16},Case{4000000000.,false,false,"N0CALL/TEST1",.352125},
            Case{0,false,false,"N0CALL/TEST1",-1,true},Case{1500,false,false,"N0CALL/TEST1",-1,true},
            Case{400000000.,false,false,"N0CALL/TEST1",-1,true},Case{4000000000.,false,false,"N0CALL/TEST1",-1,true}};
        for(const auto& test:cases) {
            const auto power=test.power;const auto only=test.only;
            const auto verify=[&](bool value,const char* message) {
                if(!value)throw Error("Helper physical Fs="+std::to_string(c.sample_rate)+
                    ", power="+std::to_string(power)+(test.pcm16?", PCM16: ":", float: ")+message);
            };
            auto input=logical;
            if(only) {modem::SampledSimulationChannel noise(c,impairment);noise.read_noise(input);}
            helper::Options options;options.power_w=power;options.data_power_w=4;options.callsign=test.callsign;
            const auto occupied=helper::occupied_bandwidth(c);
            const auto plan=helper::plan(options,c.carrier_hz,occupied,physical_rate,static_cast<double>(c.sample_rate)+250,
                modem::outer_dsss_transmit_gain(c));
            helper::Generator generator(options,plan,0);
            auto end=static_cast<long double>(eof)/c.sample_rate;
            if(test.end_phase>=0)end=.4L*std::floor(end/.4L)+test.end_phase;
            auto physical=physical_convert(input,c.sample_rate,physical_rate,[&](float payload,long double seconds,std::uint32_t rate) {
                if(test.scaled)return static_cast<float>(payload*plan.data_gain);
                return generator.mix(payload,seconds,rate,end,0);
            });
            if(test.pcm16) {
                // Native playback_pcm at gain one and native capture unpack. This
                // sampled digital loopback does not qualify analog hardware or
                // preservation at other rates, gains, geometries or weaker SNR.
                for(auto& value:physical) {
                    const auto packed=static_cast<std::int16_t>(std::clamp(value,-1.0f,1.0f)*32767);
                    value=packed/32768.0f;
                }
            }
            const auto capture=physical_convert(physical,physical_rate,c.sample_rate);
            modem::PatternSearch search;search.worker_threads=1;search.start_offset_seconds=0;search.start_uncertainty_seconds=6;
            modem::PatternReceiver receiver(c,16*1024*1024,search);
            std::map<std::pair<std::uint64_t,std::uint64_t>,Bytes> streams;
            std::vector<std::string> prefixes;std::vector<std::uint64_t> bit_polls;std::size_t completions=0;std::uint64_t complete_sample=0,last_bit_poll=0;
            for(std::size_t at=0;at<capture.size();) {
                const auto count=std::min<std::size_t>(173,capture.size()-at);
                receiver.push(std::span(capture).subspan(at,count));at+=count;
                for(auto& event:receiver.take_bursts()) {
                    auto& bits=streams[{event.stream_first_sample,event.stream_first_symbol}];
                    bits.insert(bits.end(),event.bits.begin(),event.bits.end());
                    if(!event.bits.empty()) {
                        std::string prefix;for(auto bit:bits)prefix+=bit?'1':'0';prefixes.push_back(std::move(prefix));bit_polls.push_back(at);last_bit_poll=at;
                    }
                    if(event.complete) {++completions;complete_sample=at;}
                }
            }
            if(only) {verify(streams.empty()&&!completions,"preselected helper-only tone manufactured Robust bits or completion");continue;}
            verify(streams.size()==1&&streams.begin()->second==Bytes({0,0,1})&&completions==1,
                "preselected physical helper changed exact Robust payload or completion");
            verify(prefixes==std::vector<std::string>({"0","00","001"}),"physical helper changed next-poll exact pending prefixes");
            verify(complete_sample>=last_bit_poll+6*c.sample_rate,"helper manufactured early physical payload completion");
            const auto baseline=static_cast<std::size_t>(test.pcm16);
            if(power==0) {baseline_completion[baseline]=complete_sample;baseline_bit_polls[baseline]=bit_polls;}
            else {
                verify(complete_sample==baseline_completion[baseline],"helper changed matched physical absence completion");
                verify(bit_polls==baseline_bit_polls[baseline],"helper delayed matched next-poll Robust bit progress");
            }
        }
    }
}
void helper_presentation() {
    gui::Signals signals;gui::Inbox inbox;live::Snapshot poll;
    live::SignalUpdate event;event.id=17;event.helper_carrier=true;event.frequency_hz=2450;
    event.text="Tone SNR 24 dB";event.revision=1;poll.signals={event};
    gui::apply_receptions(inbox,signals,poll);
    check(signals.lines().size()==1 && gui::signal_status_label(signals.lines().front())=="helper pending" &&
        gui::signal_records(signals).front().activatable &&
        signals.copy_text(0)==gui::signal_records(signals).front().cells[4].text,
        "helper pending presentation missing");
    event.complete=true;event.text="Tone SNR 25 dB";poll.signals={event};
    gui::apply_receptions(inbox,signals,poll);
    check(signals.lines().size()==1 && signals.lines().front().id==17 &&
        gui::signal_status_label(signals.lines().front())=="helper received", "helper replaced its stable pending row");
    check(!signals.copy_id(0)&&!signals.copy_bits(0)&&!signals.copy_raw_bits(0)&&!signals.copy_bytes(0)&&
        signals.copy_text(0)==gui::signal_records(signals).front().cells[4].text &&
        gui::signal_records(signals).front().activatable && inbox.items().empty(),
        "helper observation acquired payload copy/save or decoded-content eligibility");
    gui::SignalLine misleading=signals.lines().front();misleading.binary=true;misleading.validated=true;
    misleading.text="001";misleading.raw_bits="001";misleading.received_bits=3;misleading.reception_id="invalid";
    signals.update(misleading);
    check(!signals.lines().front().binary&&!signals.lines().front().validated&&signals.lines().front().raw_bits.empty()&&
        !signals.lines().front().received_bits&&!signals.copy_id(0),"helper imported fake payload state");
}
void helper_yields_event_queue_to_payload_progress() {
    std::vector<live::SignalUpdate> events;
    const auto append=[&](live::SignalUpdate event) {live::detail::append_signal_event(events,std::move(event),64);};
    for(const auto* prefix:{"0","00","001"}) {
        live::SignalUpdate event;event.id=1;event.text=prefix;append(event);
    }
    for(unsigned update=0;update<200;++update) {
        live::SignalUpdate event;event.id=2;event.helper_carrier=true;event.complete=update>=15;event.sequence=update;
        append(event);
    }
    check(events.size()==4 && events[0].text=="0" && events[1].text=="00" && events[2].text=="001" &&
        events.back().helper_carrier && events.back().complete && events.back().sequence==199,
        "repeated helper updates displaced unpolled exact payload prefixes");
    while(events.size()<64) {live::SignalUpdate event;event.id=100+events.size();append(event);}
    live::SignalUpdate payload;payload.id=500;payload.complete=true;append(payload);
    check(events.size()==64 && events.front().text=="0" && events[1].text=="00" && events[2].text=="001" &&
        std::none_of(events.begin(),events.end(),[](const auto& event) {return event.helper_carrier;}),
        "payload completion did not reclaim the helper queue slot first");
    const auto retained=events;
    for(unsigned update=0;update<200;++update) {
        live::SignalUpdate event;event.id=1000+update;event.helper_carrier=true;event.complete=update%2;append(event);
    }
    check(events.size()==retained.size(),"helper queue exceeded its capacity");
    for(std::size_t i=0;i<events.size();++i)
        check(events[i].id==retained[i].id && events[i].text==retained[i].text && events[i].complete==retained[i].complete,
            "helper observation evicted a full queue of payload progress or completion");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--helper-presentation-only") {
            helper_presentation();return 0;
        }
        if(argc==2 && std::string_view(argv[1])=="--rolling-helper-only") {
            rolling_helper_waits_and_drains_after_loss();return 0;
        }
        if(argc==2 && std::string_view(argv[1])=="--wide-auxiliary-only") {
            helper_only_live_simulation(true);return 0;
        }
        punctuation_requires_consumed_evidence();helper_dates_survive_capture_clear();helper_frequency_support_survives_capture_clear();helper_yields_event_queue_to_payload_progress();helper_preserves_receiver_workspace_capacity();helper_placement_covers_both_profiles();sampled_composite();helper_only_and_callsign_update();callsign_update_preserves_live_reception();helper_only_live_simulation();helper_only_live_simulation(true);helper_audit_work_credit_is_bounded();rolling_helper_waits_and_drains_after_loss();payload_survives_physical_helper_preselection();helper_presentation();
        std::cout<<"helper live integration passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
