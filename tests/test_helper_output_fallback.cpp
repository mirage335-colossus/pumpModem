#include "datapump/live.hpp"
#include "datapump/resampler.hpp"
#include "datapump/channel.hpp"
#include "../src/helper_power_reference.hpp"
#include "../src/live_helper.hpp"
#include "../src/transmit_timing.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

using namespace datapump;
using namespace std::chrono_literals;
namespace {
constexpr std::uint64_t epoch=1800000000;
void check(bool value,const char* message) {if(!value)throw Error(message);}
struct ReadObservation {
    std::uint64_t supplied=0,source_samples=0;
    std::size_t generated_bits=0;
    double key_lock_seconds=0;
    std::optional<std::uint64_t> source_end,payload_end;
};
struct Provider {
    std::atomic<unsigned> opened=0,closed=0,formats=0,source_calls=0;
    std::atomic<bool> capture_started=false;
    bool reject_helper_rate=false,fail_after_pcm=false,accept_buffered_utc=false,timing_reported=false;
    std::optional<long double> accepted_utc;
    std::uint32_t output_rate=0;
    std::vector<std::uint32_t> requested_rates;
    std::vector<float> pcm,logical_pcm;
    std::vector<ReadObservation> reads;
    std::function<live::Snapshot()> snapshot;
};
Provider* provider=nullptr;
live::Settings settings() {
    live::Settings value;value.device="helper output fixture";value.full_duplex=true;
    value.transfer.timestamp=epoch;value.transfer.search_seconds=0;
    value.transfer.automatic_receive_profiles=false;value.transfer.recovery_options.enabled=false;
    value.transfer.key.emplace(Bytes(32,0x63));
    auto& c=value.transfer.modem;
    // Keep the data band clear of the fixed550/950Hz helper tones. The
    // provider/fallback seam and ordinary source timing remain the same.
    c.sample_rate=8000;c.carrier_hz=2000;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;c.scramble=true;
    value.content_limit=1024*1024;value.dsp_workspace_bytes=32*1024*1024;
    value.helper.power_w=100;return value;
}
struct Result {
    live::Snapshot snapshot;
    unsigned opened=0,closed=0,formats=0,source_calls=0;
    bool timing_reported=false;
    std::optional<long double> accepted_utc;
    std::uint32_t output_rate=0;
    std::vector<std::uint32_t> requested_rates;
    std::vector<float> pcm,logical_pcm;
    std::vector<ReadObservation> reads;
};
Result run(live::Settings value,bool reject_rate=false,bool gap=false,bool helper_only=false,
           std::shared_ptr<const modem::FhssPlan> hopping={}) {
    Provider script;script.reject_helper_rate=reject_rate;script.fail_after_pcm=gap;
    script.accept_buffered_utc=true;
    provider=&script;
    struct Release {~Release(){provider=nullptr;}}release;
    Result result;
    {
        live::Session session([]{return static_cast<double>(epoch);});session.start(value);
        script.snapshot=[&]{return session.snapshot();};
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!script.capture_started && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(1ms);
        check(script.capture_started,"fallback fixture capture did not start");
        if(helper_only)session.transmit_helper(std::move(hopping));
        else session.transmit_bits(Bytes{0,0,1},false,std::move(hopping));
        bool finished=false;
        while(std::chrono::steady_clock::now()<deadline) {
            auto state=session.snapshot();
            if(state.transmission_id && !state.transmitting && state.transmission_finished) {
                result.snapshot=std::move(state);finished=true;break;
            }
            std::this_thread::sleep_for(1ms);
        }
        session.stop();
        check(finished,"fallback fixture did not finish the transmit request");
    } // All source/capture callbacks join before provider state is inspected.
    result.opened=script.opened;result.closed=script.closed;
    result.formats=script.formats;result.source_calls=script.source_calls;
    result.output_rate=script.output_rate;
    result.timing_reported=script.timing_reported;result.accepted_utc=script.accepted_utc;
    result.requested_rates=std::move(script.requested_rates);result.pcm=std::move(script.pcm);
    result.logical_pcm=std::move(script.logical_pcm);result.reads=std::move(script.reads);
    return result;
}
std::shared_ptr<modem::FhssPlan> fhss_hops() {
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=200;hops->dwell_seconds=.4;
    for(unsigned i=0;i<hops->channels;++i)hops->shifts[i]=30000000.+2000*i;
    return hops;
}
double calibrated_gain(const live::Settings& value,const Result& result,
                       const std::shared_ptr<const modem::FhssPlan>& hopping={}) {
    auto options=value.transfer;
    if(hopping)options.modem.fake_hopping=transfer::transmit_hopping(*hopping,options);
    const auto rate=options.modem.sample_rate;
    auto selected=options.timestamp;
    auto phase=static_cast<std::uint64_t>(options.modem.stream_phase_samples);
    if(!selected) {
        check(result.accepted_utc.has_value(),"fresh calibrated fallback did not expose its actual source UTC origin");
        const auto prefix=modem::training_sample_count(options.modem)+modem::pattern_pulse_padding_samples(options.modem);
        const auto payload=static_cast<long double>(*result.accepted_utc)+static_cast<long double>(prefix)/rate;
        selected=static_cast<std::uint64_t>(std::floor(payload));
        phase=static_cast<std::uint64_t>(std::llround((payload-selected)*rate));
        if(phase==rate) {++selected;phase=0;}
    }
    options.timestamp=selected;options.modem.stream_phase_samples=phase;
    auto source=transfer::binary_transmitter(Bytes{0,0,1},options);
    const auto config=transfer::seeded_config(options,selected);
    auto reference=helper::transmit_power_reference(config,*source,selected,selected,options.key);
    const auto origin=source->physical_output_origin();
    reference.helper_start_seconds=static_cast<long double>(origin/rate)-selected+
        static_cast<long double>(origin%rate)/rate;
    reference.helper_end_seconds=*reference.helper_start_seconds+
        static_cast<long double>(source->total_samples())/rate;
    return helper::calibrated_data_gain(value.helper,static_cast<bool>(config.fake_hopping),&reference);
}
void rate_failure_retries_ordinary() {
    auto value=settings();auto baseline=value;baseline.helper.power_w=0;
    const auto ordinary=run(baseline),fallback=run(value,true);
    check(!ordinary.snapshot.transmission_failed&&!ordinary.pcm.empty(),"ordinary fallback control failed");
    check(!fallback.snapshot.transmission_failed && fallback.snapshot.error.empty() &&
          fallback.snapshot.transmission_fraction==1,"helper provider-rate failure aborted Robust TX");
    check(fallback.opened==2 && fallback.closed==2 && fallback.formats==1 &&
          fallback.requested_rates.size()==2 && fallback.requested_rates[0]>=48000 &&
          fallback.requested_rates[1]==0,"helper rate failure did not make exactly one pre-PCM ordinary retry");
    const auto gain=calibrated_gain(value,fallback);
    check(fallback.pcm.size()==ordinary.pcm.size(),"helper decline changed the ordinary output duration");
    for(std::size_t i=0;i<fallback.pcm.size();++i)
        check(fallback.pcm[i]==static_cast<float>(ordinary.pcm[i]*gain),
              "helper decline changed the calibrated data amplitude or ordinary PCM");
    check(fallback.logical_pcm==ordinary.logical_pcm &&
          fallback.snapshot.transmit_trace.wire_bits==ordinary.snapshot.transmit_trace.wire_bits &&
          fallback.snapshot.transmit_key_lock_seconds==ordinary.snapshot.transmit_key_lock_seconds,
          "helper rate decline changed payload bytes, source PCM or keyed epoch exposure");
    check(fallback.snapshot.helper_status.find("transmitting ordinary Robust signal")!=std::string::npos,
          "ordinary output fallback omitted its helper-unavailable notice");
}
void unavailable_geometry_retains_ordinary() {
    auto value=settings();value.transfer.timestamp=epoch+10;
    value.transfer.modem.sample_rate=48000;value.transfer.modem.carrier_hz=23800;
    // The ordinary data band fits the provider's48kHz clock. A valid High
    // group requests more room, which this provider cannot actually supply.
    const auto requested=helper::minimum_output_rate(value.helper,value.transfer.modem.carrier_hz,
        helper::occupied_bandwidth(value.transfer.modem),value.transfer.modem.sample_rate);
    check(requested>48000,"unavailable High fixture unexpectedly fits its hardware clock");
    auto baseline=value;baseline.helper.power_w=0;
    const auto ordinary=run(baseline),fallback=run(value);
    check(!ordinary.snapshot.transmission_failed&&!ordinary.pcm.empty(),"helper-off geometry control failed");
    if(fallback.snapshot.transmission_failed||!fallback.snapshot.error.empty()||
       fallback.snapshot.transmission_fraction!=1)
        std::cerr<<"High placement result: error='"<<fallback.snapshot.error
                 <<"' transmission_failed="<<fallback.snapshot.transmission_failed
                 <<" fraction="<<fallback.snapshot.transmission_fraction<<" opened="<<fallback.opened
                 <<" source_calls="<<fallback.source_calls<<'\n';
    check(!fallback.snapshot.transmission_failed&&fallback.snapshot.error.empty()&&
          fallback.snapshot.transmission_fraction==1,"unavailable helper placement aborted ordinary payload");
    check(fallback.opened==1&&fallback.closed==1&&fallback.formats==1&&fallback.source_calls>0&&
          fallback.requested_rates==std::vector<std::uint32_t>{requested},
          "unavailable High placement repeated output or lost its single physical-rate request");
    const auto gain=calibrated_gain(value,fallback);
    check(fallback.pcm.size()==ordinary.pcm.size(),"unavailable placement changed ordinary output duration");
    for(std::size_t i=0;i<fallback.pcm.size();++i)
        check(fallback.pcm[i]==static_cast<float>(ordinary.pcm[i]*gain),
              "unavailable placement changed calibrated ordinary amplitude or PCM");
    check(fallback.logical_pcm==ordinary.logical_pcm&&
          fallback.snapshot.transmit_trace.wire_bits==ordinary.snapshot.transmit_trace.wire_bits&&
          fallback.snapshot.transmit_key_lock_seconds==ordinary.snapshot.transmit_key_lock_seconds,
          "unavailable placement changed ordinary source PCM, payload or key exposure");
    check(fallback.snapshot.helper_status.find("cannot fit its power-dependent leakage guard")!=std::string::npos&&
          fallback.snapshot.helper_status.find("transmitting ordinary Robust signal")!=std::string::npos,
          "unavailable placement omitted its visible helper-disabled reason");
}
void gated_geometry_retains_ordinary() {
    auto value=settings();value.transfer.timestamp=0;value.transfer.audio_timing_error_seconds=0;
    value.transfer.modem.sample_rate=48000;value.transfer.modem.carrier_hz=23900;
    value.transfer.modem.pulse_shaping=true;
    value.transfer.modem.spreading_factor=64;
    const auto hops=fhss_hops();
    auto gated=value.transfer.modem;
    gated.fake_hopping=transfer::transmit_hopping(*hops,value.transfer);
    const auto ungated_band=helper::occupied_bandwidth(value.transfer.modem);
    const auto occupied=helper::occupied_bandwidth(gated);
    check(ungated_band==125&&occupied==325&&gated.carrier_hz+ungated_band/2<24000&&
          gated.carrier_hz+occupied/2>24000,
          "gated fallback fixture lost its ordinary-valid RRC and uncapturable gate skirt");
    const auto requested=helper::minimum_output_rate(value.helper,helper::placement_band(gated),gated.sample_rate);
    auto baseline=value;baseline.helper.power_w=0;
    const auto ordinary=run(baseline,false,false,false,hops),fallback=run(value,false,false,false,hops);
    check(!ordinary.snapshot.transmission_failed&&ordinary.snapshot.error.empty()&&!ordinary.pcm.empty(),
          "ordinary gated near-Nyquist control failed");
    if(fallback.snapshot.transmission_failed||!fallback.snapshot.error.empty()||
       fallback.snapshot.transmission_fraction!=1)
        std::cerr<<"Gated placement result: error='"<<fallback.snapshot.error
                 <<"' transmission_failed="<<fallback.snapshot.transmission_failed
                 <<" fraction="<<fallback.snapshot.transmission_fraction<<" opened="<<fallback.opened
                 <<" source_calls="<<fallback.source_calls<<'\n';
    check(!fallback.snapshot.transmission_failed&&fallback.snapshot.error.empty()&&
          fallback.snapshot.transmission_fraction==1,"gated helper placement aborted ordinary payload");
    check(fallback.opened==1&&fallback.closed==1&&fallback.formats==1&&fallback.source_calls>0&&
          fallback.requested_rates==std::vector<std::uint32_t>{requested},
          "unavailable gated placement repeated output or lost its physical-rate request");
    const auto gain=calibrated_gain(value,fallback,hops);
    check(fallback.pcm.size()==ordinary.pcm.size(),"gated fallback changed ordinary output duration");
    for(std::size_t i=0;i<fallback.pcm.size();++i)
        check(fallback.pcm[i]==static_cast<float>(ordinary.pcm[i]*gain),
              "gated fallback changed calibrated hardware PCM");
    check(fallback.logical_pcm==ordinary.logical_pcm&&
          fallback.snapshot.transmit_trace.wire_bits==ordinary.snapshot.transmit_trace.wire_bits&&
          fallback.snapshot.transmit_key_lock_seconds==ordinary.snapshot.transmit_key_lock_seconds,
          "gated fallback changed ordinary source, payload or keyed exposure");
    check(fallback.snapshot.helper_status.find("requires a valid data passband at the output sample rate")!=std::string::npos&&
          fallback.snapshot.helper_status.find("transmitting ordinary Robust signal")!=std::string::npos,
          "gated fallback omitted the original passband decline reason");
}
void malformed_helper_settings_are_rejected() {
    for(const bool invalid_power:{true,false}) {
        auto value=settings();
        if(invalid_power)value.helper.power_w=-1;
        else {value.transfer.key.reset();value.helper.callsign="ABCDEFGHIJKLMNOPQRST";}
        bool rejected=false;
        try {live::validate_settings(value);}
        catch(const Error& error) {
            const auto message=std::string_view(error.what());
            rejected=invalid_power?message=="invalid Helper Carrier power or ratio limit":
                message=="Helper Carrier callsign must contain at most 16 seven-bit ASCII characters";
        }
        check(rejected,"placement fallback accepted malformed helper settings");
    }
    for(const auto placement:{helper::Frequency::low,helper::Frequency::cochannel}) {
        auto value=settings();value.helper.frequency=placement;bool rejected=false;
        try {live::validate_settings(value);}
        catch(const Error& error) {rejected=std::string_view(error.what())=="Helper Carrier requires High frequency placement";}
        check(rejected,"live settings accepted a retired Helper placement");
    }
}
void emitted_gap_is_never_retried() {
    const auto result=run(settings(),false,true);
    check(result.snapshot.transmission_failed && result.snapshot.error=="fixture PCM discontinuity" &&
          result.snapshot.transmission_fraction<1,"actual output gap lost its latched TX failure");
    check(result.opened==1 && result.closed==1 && result.source_calls>0 && !result.pcm.empty(),
          "actual output gap restarted private PCM or occurred before any generated output");
    check(result.snapshot.transmit_key_lock_seconds>0,"actual output gap released the exposed private key epoch");
}
void helper_only_cannot_succeed_without_helper() {
    auto value=settings();value.transfer.timestamp=epoch+10;
    value.transfer.modem.sample_rate=48000;value.transfer.modem.carrier_hz=23800;
    const auto result=run(value,false,false,true);
    check(result.snapshot.transmission_failed && !result.snapshot.error.empty() &&
          result.snapshot.transmission_fraction==0,"invalid helper-only request pretended to send a beacon");
    check(result.opened==1 && result.closed==1 && result.source_calls==0 && result.pcm.empty(),
          "unavailable helper-only request emitted ordinary payload, silence or a replay");
}
void explicit_past_declines_at_calibrated_amplitude() {
    auto value=settings();auto baseline=value;baseline.helper.power_w=0;
    const auto ordinary=run(baseline),declined=run(value);
    const auto gain=calibrated_gain(value,declined);
    check(!declined.snapshot.transmission_failed && declined.snapshot.transmission_fraction==1 &&
          declined.opened==1 && declined.logical_pcm==ordinary.logical_pcm,
          "past explicit helper epoch changed or aborted the ordinary source");
    check(declined.pcm.size()==ordinary.pcm.size(),"past explicit helper epoch inserted a lead or tail");
    for(std::size_t i=0;i<ordinary.pcm.size();++i)
        check(declined.pcm[i]==static_cast<float>(ordinary.pcm[i]*gain),
              "past explicit helper epoch lost the calibrated ordinary amplitude");
    check(declined.snapshot.transmit_trace.wire_bits==ordinary.snapshot.transmit_trace.wire_bits &&
          declined.snapshot.transmit_key_lock_seconds==ordinary.snapshot.transmit_key_lock_seconds &&
          declined.snapshot.helper_status.find("lead does not fit")!=std::string::npos,
          "past explicit helper epoch changed key exposure or omitted its decline reason");
}
void hardware_channel_helper_transform() {
    auto plain_settings=settings();plain_settings.transfer.timestamp=epoch+100;
    auto stressed_settings=plain_settings;stressed_settings.hardware_channel_test=true;
    stressed_settings.simulation_spread_delay_ms=250;stressed_settings.simulation_spread_doppler_hz=.1;
    stressed_settings.simulation_perturbation_slow_depth=stressed_settings.simulation_perturbation_fast_depth=.65;
    modem::ChannelConfig effects;effects.spread_delay_ms=250;effects.spread_doppler_hz=.1;
    effects.perturbation_slow_depth=effects.perturbation_fast_depth=.65;
    const modem::HardwareTestChannel stage(stressed_settings.transfer.modem,effects);
    const auto plain=run(plain_settings),stressed=run(stressed_settings);
    check(!plain.snapshot.transmission_failed && !stressed.snapshot.transmission_failed &&
          plain.snapshot.transmission_fraction==1 && stressed.snapshot.transmission_fraction==1,
          "Helper hardware channel transmission did not complete");
    check(plain.accepted_utc && stressed.accepted_utc && plain.accepted_utc==stressed.accepted_utc,
          "Helper comparison changed the planned source clock");
    check(!stressed.requested_rates.empty() &&
          stressed.output_rate>=*std::max_element(stressed.requested_rates.begin(),stressed.requested_rates.end()),
          "Helper fixture did not satisfy the requested physical sample-rate floor");
    const auto logical_rate=stressed_settings.transfer.modem.sample_rate;
    const auto lead=38ULL*logical_rate;
    check(plain.logical_pcm.size()>lead && stressed.logical_pcm.size()>lead &&
          std::all_of(plain.logical_pcm.begin(),plain.logical_pcm.begin()+lead,[](float x){return x==0;}) &&
          std::all_of(stressed.logical_pcm.begin(),stressed.logical_pcm.begin()+lead,[](float x){return x==0;}),
          "Helper channel test lost the fixed observation lead");
    // These physical samples contain Helper alone; the real output transform
    // must apply the same fixed headroom as it applies to DATA.
    const auto first=plain.output_rate;const auto last=2ULL*plain.output_rate;
    check(last<plain.pcm.size() && last<stressed.pcm.size(),"Helper leading comparison is unavailable");
    double reference_energy=0,error=0;
    for(auto i=first;i<last;++i) {
        reference_energy+=double(plain.pcm[i])*plain.pcm[i];
        const auto difference=double(stressed.pcm[i])-stage.headroom_gain()*plain.pcm[i];error+=difference*difference;
    }
    check(reference_energy>1e-8 && error<reference_energy*1e-10,
          "physical Helper transform did not preserve composite headroom");
    const auto last_signal=[](const auto& pcm) {
        const auto last=std::find_if(pcm.rbegin(),pcm.rend(),[](float x){return std::abs(x)>1e-8F;});
        return last==pcm.rend()?std::size_t{0}:pcm.size()-static_cast<std::size_t>(last-pcm.rbegin());
    };
    check(last_signal(stressed.logical_pcm)>=last_signal(plain.logical_pcm)+logical_rate/4-16,
          "Helper wrapper discarded the delayed ordinary signal");
    check(stressed.logical_pcm.size()>plain.logical_pcm.size() && stressed.pcm.size()>plain.pcm.size(),
          "Helper physical output did not extend for the channel tail");
    check(std::all_of(stressed.pcm.begin(),stressed.pcm.end(),[](float x){return std::isfinite(x) && std::abs(x)<=1;}),
          "Helper channel composition exceeded hardware full scale");
    check(!stressed.reads.empty() && stressed.reads.back().source_end==stressed.logical_pcm.size(),
          "Helper protected source end omitted the channel tail");
}

void hardware_helper_envelope(bool fresh) {
    // The explicit timestamp case must leave enough future time to emit the
    // complete fixed38-second lead before its payload origin.
    auto value=settings();value.transfer.timestamp=fresh?0:epoch+100;
    auto options=value.transfer;
    const auto rate=options.modem.sample_rate;
    for(const auto origin:{std::uint64_t{0},std::uint64_t{1},static_cast<std::uint64_t>(rate/3),
            static_cast<std::uint64_t>(3ULL*rate+rate/2)})
        check(live::detail::helper_leading_seconds(origin,rate,38)==38,
              "partial first dwell rounded the fixed helper lead upward");
    std::unique_ptr<modem::StreamingTransmitter> reference;
    if(fresh) {
        audio::detail::UtcPlaybackScope buffered(true);
        const auto margin=audio::minimum_lead_seconds()+38.4+1./options.modem.sample_rate;
        auto scheduled=detail::schedule_transmission(options.modem,[&](std::uint64_t selected,std::uint32_t phase) {
            options.timestamp=selected;options.modem.stream_epoch=selected;options.modem.stream_phase_samples=phase;
            return transfer::binary_transmitter(Bytes{0,0,1},options);
        },[]{return static_cast<double>(epoch);},{},0,margin);
        reference=std::move(scheduled.transmitter);
    } else reference=transfer::binary_transmitter(Bytes{0,0,1},options);
    const auto total=reference->total_samples();
    const auto physical=reference->physical_output_origin();
    const auto result=run(value);
    check(!result.snapshot.transmission_failed && result.snapshot.transmission_fraction==1 &&
          result.accepted_utc && result.opened==1 && !result.reads.empty(),
          "hardware helper envelope did not complete one scheduled waveform");
    const auto first=std::find_if(result.reads.begin(),result.reads.end(),[](const auto& read){return read.source_samples>0;});
    check(first!=result.reads.end() && first->supplied>=first->source_samples,"hardware helper envelope never read its source");
    const auto lead=first->supplied-first->source_samples;
    check(lead==38ULL*rate && result.logical_pcm.size()>=lead+total+2ULL*rate,
          "hardware helper envelope did not retain exactly 95 dwells and the two-second minimum tail");
    const auto payload_epoch=static_cast<long double>(physical/rate)+static_cast<long double>(physical%rate)/rate;
    check(std::abs(*result.accepted_utc+static_cast<long double>(lead)/rate-payload_epoch)<1e-6L,
          "hardware helper lead shifted the already planned keyed payload epoch");
    if(fresh)check(*result.accepted_utc>epoch+2,"fresh scheduling did not reserve the helper before payload preparation");
    check(std::all_of(result.logical_pcm.begin(),result.logical_pcm.begin()+lead,[](float sample){return sample==0;}),
          "hardware helper lead read or emitted ordinary source PCM");
    std::uint64_t consumed=0;std::optional<std::uint64_t> endpoint;
    std::array<float,512> expected{};
    bool observed_lead=false,observed_post=false;
    for(const auto& read:result.reads) {
        const auto wanted=std::min(total,read.supplied>lead?read.supplied-lead:0);
        check(read.source_samples==wanted,"hardware envelope lead or tail inflated ordinary payload progress");
        if(!read.source_samples) {
            observed_lead=true;
            check(!read.generated_bits && read.key_lock_seconds==0,"helper-only lead exposed private source bits or key epochs");
        }
        if(read.supplied>lead+total)observed_post=true;
        while(consumed<wanted) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(expected.size(),wanted-consumed));
            check(reference->read(std::span(expected).first(count))==count,"independent ordinary source ended early");
            check(std::equal(expected.begin(),expected.begin()+count,result.logical_pcm.begin()+lead+consumed),
                  "hardware helper envelope changed ordinary source waveform or keyed epoch");
            consumed+=count;
        }
        check(read.payload_end && *read.payload_end==lead+reference->predicted_physical_valid_payload_end(
              read.supplied>lead?read.supplied-lead:0),"hardware helper lead was not mapped in protected payload frontiers");
        check(read.source_end && *read.source_end==result.logical_pcm.size(),"hardware helper endpoint disagreed with physical EOF");
        if(endpoint)check(*read.source_end<=*endpoint,"hardware helper endpoint grew after source preparation");
        endpoint=read.source_end;
    }
    check(observed_lead&&observed_post&&consumed==total&&reference->finished(),
          "hardware helper envelope failed to retain its entire ordinary source and independent cover");
    check(std::all_of(result.logical_pcm.begin()+lead+total,result.logical_pcm.end(),[](float sample){return sample==0;}),
          "hardware helper post-roll advanced or repeated the ordinary source");
    const auto tail=result.logical_pcm.size()-lead-total;
    const auto absence=modem::pattern_absence_capture_samples(options.modem)+rate+
        2*modem::pattern_pulse_padding_samples(options.modem);
    check(tail>=absence && tail>=result.snapshot.receiver_initial_fft_input_samples+rate+128,
          "hardware helper post-roll did not cover physical absence and the immutable receiver aperture");
    check(result.snapshot.transmission_seconds==static_cast<double>(total)/rate &&
          result.snapshot.transmit_trace.wire_bits==reference->transmit_trace().wire_bits &&
          result.snapshot.transmit_trace.pattern_output==reference->transmit_trace().pattern_output,
          "hardware helper changed final ordinary progress or exact keyed payload bits");
    double energy=0;
    const auto samples=std::min<std::size_t>(result.pcm.size(),result.output_rate);
    for(std::size_t i=0;i<samples;++i)energy+=result.pcm[i]*result.pcm[i];
    check(samples&&energy/samples>.001,"helper-only lead did not contain actual physical carrier PCM");
}
void fhss_output_uses_one_gate_allowance() {
    auto value=settings();value.transfer.timestamp=0;value.transfer.audio_timing_error_seconds=0;
    value.transfer.modem.pulse_shaping=true;value.transfer.modem.spreading_factor=64;
    const auto hops=fhss_hops();
    auto expected_config=value.transfer.modem;
    expected_config.fake_hopping=transfer::transmit_hopping(*hops,value.transfer);
    // Production FHSS needs finite pulse support. At 8 kHz and a 200 Hz
    // nominal rate the 80-sample chip has 1.25*8000/80 = 125 Hz RRC support;
    // its single 10 ms gate contributes another 200 Hz, for 325 Hz total.
    check(modem::waveform_gate_bandwidth_allowance_hz(expected_config)==200 &&
          helper::occupied_bandwidth(expected_config)==325,
          "FHSS helper fixture does not contain exactly one 200 Hz gate allowance");
    const auto common=helper::plan(value.helper,expected_config.carrier_hz,
        helper::occupied_bandwidth(expected_config),48000,0,
        modem::outer_dsss_transmit_gain(expected_config));
    const auto result=run(value,false,false,false,hops);
    check(!result.snapshot.transmission_failed && result.snapshot.error.empty() &&
          result.opened==1 && result.output_rate==48000 && result.pcm.size()>=48000,
          "FHSS helper fixture did not render one second of actual hardware-rate PCM");
    check(result.timing_reported && result.accepted_utc && *result.accepted_utc>epoch,
          "FHSS mock did not accept its fresh buffered UTC target");
    // Estimate the emitted carrier from real output samples. Independent 40 ms
    // coherent fits measure the pure pilot separately from auxiliary metadata; this
    // is not a second call through the output-transform/configuration logic.
    const auto window=result.output_rate/25;
    const auto count=std::min<std::size_t>(result.pcm.size(),result.output_rate);
    double best_frequency=0,best_power=-1;
    for(int offset=-150;offset<=150;++offset) {
        const auto frequency=common.frequency_hz+offset;
        const auto step=std::polar(1.,-2*std::numbers::pi*frequency/result.output_rate);
        std::complex<double> oscillator{1,0},sum{};double power=0;
        for(std::size_t i=0;i<count;++i) {
            sum+=static_cast<double>(result.pcm[i])*oscillator;oscillator*=step;
            if((i+1)%window==0) {power+=std::norm(sum);sum={};}
        }
        if(power>best_power) {best_power=power;best_frequency=frequency;}
    }
    check(std::abs(best_frequency-common.frequency_hz)<=1,
          "FHSS emitted helper frequency disagrees with the common single-gate plan");
}
}

// Same existing link seam as live_transmit_lock/live_profiles: this fixture
// replaces device I/O, while Session, source generation, output-transform
// composition, resampling, completion and key exposure remain production code.
namespace datapump::audio {
std::vector<Device> devices(){return {{"helper output fixture","bounded synthetic output"}};}
void capture(std::uint32_t rate,const std::string&,const CaptureCallback&,std::stop_token stop,
             StreamFormatCallback format) {
    if(format)format({rate,48000,rate*.5,0});
    provider->capture_started=true;
    while(!stop.stop_requested())std::this_thread::sleep_for(1ms);
}
void capture(std::uint32_t rate,const std::string& device,const CaptureCallback& callback,std::stop_token stop,
             StreamFormatCallback format,Options) {
    capture(rate,device,callback,stop,std::move(format));
}
void playback(std::uint32_t logical,const std::string&,const PlaybackCallback& source,std::stop_token stop,
              StreamFormatCallback format,ChannelMode,Options options) {
    validate_options(options);auto& script=*provider;
    ++script.opened;script.requested_rates.push_back(options.minimum_hardware_rate);
    struct Close {Provider& script;~Close(){++script.closed;}}close{script};
    if(script.reject_helper_rate&&options.minimum_hardware_rate)
        throw Error("fixture helper hardware rate unavailable");
    const auto hardware=48000u;
    script.output_rate=hardware;
    Resampler converter(logical,hardware);
    converter.set_output_gain(options.playback_output_gain);
    converter.set_output_transform(options.playback_output_transform);
    // Like the existing timed-provider fixture, accept the synthetic UTC
    // presentation target without waiting against the host's unrelated clock.
    // This accepts presentation targets without using a physical device.
    detail::UtcPlaybackScope schedule(script.accept_buffered_utc);
    if(script.accept_buffered_utc) {
        check(bool(options.timing_status),"FHSS mock omitted its timing status callback");
        options.timing_status({options.follow_system_clock,.05,.03,"fixture buffered UTC target accepted"});
        script.timing_reported=true;
    }
    if(format) {++script.formats;format({logical,hardware,logical*.5,converter.workspace_bytes()});}
    if(script.accept_buffered_utc)script.accepted_utc=detail::utc_playback_epoch;
    std::array<float,512> input{},output{};std::size_t offset=0,count=0;bool ending=false;
    while(!stop.stop_requested()&&!converter.finished()) {
        if(offset==count&&!ending) {
            count=source(input);++script.source_calls;offset=0;ending=count==0;
            check(count<=input.size(),"fixture source exceeded its supplied output extent");
            script.logical_pcm.insert(script.logical_pcm.end(),input.begin(),input.begin()+count);
            const auto state=script.snapshot();
            ReadObservation observed;
            observed.supplied=script.logical_pcm.size();
            observed.source_samples=static_cast<std::uint64_t>(std::llround(state.transmission_seconds*logical));
            observed.generated_bits=state.transmit_trace.generated_bits;
            observed.key_lock_seconds=state.transmit_key_lock_seconds;
            if(options.timing_source_end)observed.source_end=options.timing_source_end();
            if(options.timing_protected_payload_end)observed.payload_end=options.timing_protected_payload_end(observed.supplied);
            script.reads.push_back(observed);
        }
        const auto progress=converter.process(std::span<const float>(input).subspan(offset,count-offset),output,ending);
        offset+=progress.consumed;
        script.pcm.insert(script.pcm.end(),output.begin(),output.begin()+progress.produced);
        if(progress.produced&&script.fail_after_pcm) {
            check(bool(options.playback_discontinuity),"private source omitted its output discontinuity hook");
            options.playback_discontinuity("fixture PCM discontinuity");
            throw Error("private output discontinuity hook failed to stop playback");
        }
    }
    if(stop.stop_requested())throw Error("audio operation cancelled");
}
void playback(std::uint32_t rate,const std::string& device,const PlaybackCallback& source,std::stop_token stop,
              StreamFormatCallback format,ChannelMode channels) {
    playback(rate,device,source,stop,std::move(format),channels,{});
}
void play(std::span<const float>,std::uint32_t,const std::string&,std::stop_token,StreamFormatCallback,ChannelMode) {
    throw Error("unexpected direct playback in helper fallback fixture");
}
std::vector<float> record(double,std::uint32_t,const std::string&,std::size_t,std::stop_token,StreamFormatCallback) {
    throw Error("unexpected record in helper fallback fixture");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=1) {
            if(argc!=2)throw Error("expected one focused Helper selector");
            const auto selector=std::string_view(argv[1]);
            if(selector=="--placement-high") {
                unavailable_geometry_retains_ordinary();
                std::cout<<"High placement fallback subset passed\n";
            } else if(selector=="--placement-gated") {
                gated_geometry_retains_ordinary();
                std::cout<<"Gated placement fallback subset passed\n";
            } else if(selector=="--hardware-channel") {
                hardware_channel_helper_transform();
                std::cout<<"Helper hardware channel transform passed\n";
            } else throw Error("expected --placement-high, --placement-gated or --hardware-channel");
            return 0;
        }
        malformed_helper_settings_are_rejected();rate_failure_retries_ordinary();unavailable_geometry_retains_ordinary();gated_geometry_retains_ordinary();emitted_gap_is_never_retried();
        helper_only_cannot_succeed_without_helper();
        explicit_past_declines_at_calibrated_amplitude();
        hardware_helper_envelope(false);hardware_helper_envelope(true);
        hardware_channel_helper_transform();
        fhss_output_uses_one_gate_allowance();
        std::cout<<"Helper calibrated decline, exact keyed PCM, hardware envelope, protected frontiers, discontinuity and FHSS placement passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
