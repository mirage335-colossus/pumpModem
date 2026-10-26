#include "datapump/channel.hpp"
#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include "../src/live_helper.hpp"
#include "../src/live_signal_queue.hpp"
#include "../src/gui/state.hpp"
#include "../src/gui/record_presentations.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>
#include <map>
#include <cstdint>
using namespace datapump;
namespace {
void check(bool value,const char* what) {if(!value)throw Error(what);}
modem::Config config() {
    modem::Config c;c.sample_rate=8000;c.bandwidth_hz=1000;c.carrier_hz=1500;
    c.spreading_factor=16;c.stream_epoch=1800000000;return c;
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
    check(plan.frequency_hz-150>longer.carrier_hz+width/2,
        "helper overlapped long-message data band");
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
void helper_only_live_simulation() {
    live::Settings settings;settings.simulation=true;settings.full_duplex=true;settings.transfer.modem=config();
    settings.transfer.timestamp=1800000000;settings.transfer.search_seconds=0;settings.simulation_snr_db=100;
    settings.simulation_clock_error_ppm=0;settings.simulation_phase_noise_degrees_per_sqrt_second=0;
    settings.dsp_workspace_bytes=32*1024*1024;settings.content_limit=1024*1024;
    settings.helper.power_w=1500;settings.helper.callsign="N0CALL";
    live::Session session;session.start(settings);session.transmit_helper();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    bool helper_row=false,callsign=false;std::uint64_t helper_id=0;
    do {
        const auto snapshot=session.snapshot();check(snapshot.error.empty(),"helper-only Live simulation failed");
        check(snapshot.received.empty(),"helper-only Live simulation manufactured received payload content");
        for(const auto& event:snapshot.signals) {
            check(event.helper_carrier||!event.received_bits,"helper-only Live simulation manufactured binary bits");
            if(!event.helper_carrier)continue;
            helper_row=true;if(!helper_id)helper_id=event.id;
            check(event.id==helper_id,"ongoing Live helper replaced its stable signal row");
            callsign=callsign||event.text.find("N0CALL")!=std::string::npos;
        }
        if(helper_row&&callsign&&snapshot.helper_frequency_offset_hz)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(std::chrono::steady_clock::now()<deadline);
    check(helper_row,"helper-only physical simulation did not publish a received helper row");
    check(callsign,"strong coherent Live helper did not decode its callsign metadata");
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
            Case{4000000000.,false,false,"N0CALL/TEST1",.24},Case{4000000000.,false,false,"N0CALL/TEST1",.352125},
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
    check(signals.lines().size()==1 && gui::signal_status_label(signals.lines().front())=="helper pending",
        "helper pending presentation missing");
    event.complete=true;event.text="Tone SNR 25 dB";poll.signals={event};
    gui::apply_receptions(inbox,signals,poll);
    check(signals.lines().size()==1 && signals.lines().front().id==17 &&
        gui::signal_status_label(signals.lines().front())=="helper received", "helper replaced its stable pending row");
    check(!signals.copy_id(0)&&!signals.copy_bits(0)&&!signals.copy_raw_bits(0)&&!signals.copy_bytes(0)&&
        !signals.copy_text(0)&&!gui::signal_records(signals).front().activatable && inbox.items().empty(),
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
int main() {
    try {helper_dates_survive_capture_clear();helper_yields_event_queue_to_payload_progress();helper_preserves_receiver_workspace_capacity();helper_placement_covers_both_profiles();sampled_composite();helper_only_and_callsign_update();callsign_update_preserves_live_reception();helper_only_live_simulation();payload_survives_physical_helper_preselection();helper_presentation();
        std::cout<<"helper live integration passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
