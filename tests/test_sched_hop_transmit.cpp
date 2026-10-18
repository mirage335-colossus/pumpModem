#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"
#include "../src/transmit_epoch_guard.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <string>

using namespace datapump;
namespace {
using Complex=std::complex<double>;
void check(bool value,const char* message) { if(!value)throw Error(message); }
modem::Config configuration(unsigned factor,unsigned multiplier=10,unsigned active=1,std::uint32_t fs=512) {
    transfer::Options options;options.key.emplace(Bytes(32,0x67));options.timestamp=1800000321;
    auto& c=options.modem;c.sample_rate=fs;c.carrier_hz=fs/4.;c.bandwidth_hz=(fs/4.)/factor;
    c.integration_seconds=1.5;c.scramble=c.dsss=true;c.dsss_factor=factor;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    auto plan=std::make_shared<modem::SchedHopPlan>();plan->time_multiplier=multiplier;plan->group_hops=active;
    plan->clock_error_seconds=.001;plan->peer_clock_error_seconds=.001;
    plan->audio_error_seconds=.01;plan->ramp_seconds=.01;c.scheduled_hopping=plan;
    return transfer::seeded_config(options,options.timestamp);
}
modem::Config aligned(modem::Config c,bool surrounding=true) {
    const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto request=c.stream_epoch*c.sample_rate;
    const auto output=map.source_to_physical(map.locate(request).source);
    const auto prefix=(surrounding?modem::training_sample_count(c):0)+modem::pattern_pulse_padding_samples(c);
    auto source=map.locate(output).source.add_samples(prefix);
    if(modem::packed_scheduled_symbols(c)) {
        const modem::ScheduledSymbolCalendar calendar(c);
        source={calendar.source_slot(calendar.first_rank_source(source.sample)),0};
    }
    const auto payload=map.source_to_physical(source);
    c.stream_epoch=payload.sample/c.sample_rate;c.stream_phase_samples=payload.sample%c.sample_rate;
    return c;
}
std::vector<Complex> capture(modem::PatternTransmitter& tx,std::size_t block=137) {
    check(tx.total_samples()<2000000,"fixture would exceed bounded capture scope");
    std::vector<Complex> result(tx.total_samples());std::size_t offset=0;
    while(!tx.finished()) {
        const auto n=tx.read_analytic(std::span(result).subspan(offset,std::min(block,result.size()-offset)));
        check(n!=0,"scheduled transmitter stopped before its endpoint");offset+=n;
    }
    return result;
}
void split_waveform() {
    for(const auto fs:{64U,512U})for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=aligned(configuration(factor,10,2,fs),false);
        const Bytes bits{0,1,0};modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false),fragmented(bits,c,c.stream_epoch,0,false);
        const auto actual=capture(tx,211),pieces=capture(fragmented,13);
        check(actual.size()==pieces.size(),"scheduled block rendering changed its extent");
        for(std::size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-pieces[i])<1e-9,
              "scheduled PCM or carrier phase depends on output block boundaries");
        const auto source_total=bits.size()*modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c);
        check(tx.source_total_samples()==source_total && tx.source_samples_emitted()==source_total,
              "scheduled null time consumed source samples");
        check(tx.total_samples()==modem::pattern_transmission_sample_count(c,bits.size(),false),
              "duration helper disagrees with split transmitter");
        check(tx.working_bytes()<=c.memory_limit,"scheduled maps exceed transmitter workspace");
        const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        modem::PatternCode code(c,c.stream_epoch);const auto pad=modem::pattern_pulse_padding_samples(c);
        const auto origin=tx.physical_output_origin();const auto source_origin=map.locate(origin).source;
        const auto gain=std::sqrt(2*modem::nominal_signal_power)*modem::outer_dsss_transmit_gain(c);
        // Ordinary rectangular chips retain their linear source values. Only
        // shaped overlap or packed source composition selects the final limiter.
        const bool source_limited=pad||modem::packed_scheduled_symbols(c);
        std::size_t nulls=0,lit=0;
        for(std::uint64_t i=0;i<actual.size();++i) {
            const auto where=map.locate(origin+i);
            if(!where.active) {check(actual[i]==Complex{},"null hop emitted private waveform or cover");++nulls;continue;}
            const auto u=where.source.distance(source_origin);Complex base{};
            for(std::size_t bit=0;bit<bits.size();++bit)
                base+=code.shaped_value(bit*code.chips_per_symbol(),bits[bit],static_cast<double>(u-pad-bit*code.symbol_samples()));
            const auto oscillator=std::polar(1.,2*std::numbers::pi*c.carrier_hz*(static_cast<double>(i)/c.sample_rate));
            const auto signal=gain*base;
            const auto source=source_limited?modem::pattern_limit_pcm(signal):signal;
            const auto expected=modem::SchedHopSourceMap::gain(where,{origin+i})*oscillator*source;
            if(!(std::abs(actual[i]-expected)<2e-8)) {
                const auto envelope=modem::SchedHopSourceMap::gain(where,{origin+i});
                const auto raw=envelope*oscillator*(gain*base);
                std::cerr<<std::setprecision(std::numeric_limits<double>::max_digits10)
                    <<"SPLIT_WAVEFORM_MISMATCH fs="<<c.sample_rate<<" factor="<<c.dsss_factor
                    <<" sample="<<i<<" source_sample="<<u<<" chip="<<code.chip_samples()
                    <<" symbol="<<code.symbol_samples()<<" padding="<<pad
                    <<" shaped="<<modem::pattern_pulse_enabled(c)
                    <<" packed="<<modem::packed_scheduled_symbols(c)<<" source_limited="<<source_limited<<" envelope="<<envelope
                    <<" source_radius="<<std::abs(gain*base)<<" limit="<<modem::pattern_pcm_radius_limit
                    <<" actual="<<actual[i]<<" raw="<<raw<<" expected="<<expected
                    <<" raw_error="<<std::abs(actual[i]-raw)
                    <<" expected_error="<<std::abs(actual[i]-expected)
                    <<" potential_cap_error="<<envelope*std::max(0.,std::abs(gain*base)-modem::pattern_pcm_radius_limit)
                    <<std::endl;
            }
            check(std::abs(actual[i]-expected)<2e-8,"split waveform lost source shaping, independent patterns, or physical carrier phase");
            lit+=std::abs(actual[i])>1e-10;
        }
        check(nulls && lit,"split fixture did not cover active and null intervals");
        for(std::uint64_t bit=0;bit<bits.size();++bit) {
            const auto address=modem::symbol_stream_address(c,c.stream_epoch,c.stream_phase_samples,bit);
            const auto anchor=map.locate(c.stream_epoch*c.sample_rate+c.stream_phase_samples).source;
            const auto physical=map.source_to_physical(anchor.add_samples(bit*code.symbol_samples()));
            check(address.epoch==physical.sample/c.sample_rate && address.sample_in_second==physical.sample%c.sample_rate,
                  "pattern/Data epoch used virtual seconds instead of actual symbol UTC");
        }
    }
}
void continuous_reference() {
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        for(const auto multiplier:{3U,10U}) for(const auto fs_delta:{0U,1U}) for(const bool fhss:{false,true}) {
            auto c=configuration(factor,multiplier,multiplier);c.stream_phase_samples=17;
            c.sample_rate+=fs_delta;
            if(fhss) {
                auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=2;hopping->dwell_seconds=.4;
                hopping->shifts[0]=30000000;hopping->shifts[1]=30100000;
                hopping->power_ramps=hopping->mandatory_ramps=hopping->smooth_ramps=true;
                hopping->clock_error_seconds=hopping->peer_clock_error_seconds=.01;
                hopping->audio_error_seconds=.05;hopping->region_seconds=.001;hopping->ramp_seconds=.01;
                c.fake_hopping=std::move(hopping);
            }
            auto original=c;original.scheduled_hopping.reset();
            modem::PatternTransmitter a(Bytes{0,1},c,c.stream_epoch),b(Bytes{0,1},original,original.stream_epoch);
            check(capture(a)==capture(b),"continuous dilution changed the preceding waveform/FHSS ramps");
            check(modem::canonical_phase_step(c)==modem::canonical_phase_step(original),
                  "continuous dilution introduced scheduled canonical-phase cells");
        }
    }
}
void temporary_noise_and_finish() {
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor);
        if(factor==10 || factor==100) {
            auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=2;hopping->dwell_seconds=.4;
            hopping->shifts[0]=30000000;hopping->shifts[1]=30100000;
            hopping->power_ramps=hopping->mandatory_ramps=hopping->smooth_ramps=true;
            hopping->clock_error_seconds=hopping->peer_clock_error_seconds=.01;
            hopping->audio_error_seconds=.05;hopping->region_seconds=.001;hopping->ramp_seconds=.01;
            c.fake_hopping=hopping;
        }
        c=modem::NoiseTransmitter::prepare_config(c);c=aligned(c);
        modem::NoiseTransmitter noise(c,modem::NoiseTransmitter::PreparedTemporaryKeys{},2);
        modem::PatternTransmitter ordinary(c,modem::PatternTransmitter::MaskedZeroBits{2},c.stream_epoch);
        check(noise.total_samples()==ordinary.total_samples(),"noise changed the common keyed schedule");
        std::array<Complex,113> left{},right{};
        while(!noise.finished()) {
            const auto a=noise.read_analytic(left),b=ordinary.read_analytic(right);
            check(a==b && std::equal(left.begin(),left.begin()+a,right.begin()),"prepared noise bypassed the ordinary full-DSSS split engine");
        }
        modem::PatternTransmitter stopped(Bytes{0,1,0,1},c,c.stream_epoch);
        std::array<Complex,37> buffer{};
        const auto target=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c)+modem::pattern_chip_samples(c)/2;
        while(stopped.source_samples_emitted()<target)stopped.read_analytic(buffer);
        const modem::SchedHopSourceMap null_map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        while(null_map.locate(stopped.physical_output_origin()+stopped.samples_emitted()).active)
            stopped.read_analytic(std::span(buffer).first(1));
        const auto paused=stopped.source_samples_emitted();
        stopped.read_analytic(std::span(buffer).first(1));
        check(stopped.source_samples_emitted()==paused,"null playback advanced the private source clock");
        const auto at=stopped.samples_emitted(),source_at=stopped.source_samples_emitted();
        const auto predicted_valid=stopped.predicted_physical_valid_payload_end(at);
        const auto predicted_stop=stopped.predicted_physical_stop_end(at);
        const auto original=stopped.original_total_samples();stopped.request_finish();
        check(stopped.original_total_samples()==original,"graceful stop rewound the immutable key exposure extent");
        check(stopped.physical_valid_payload_end()==predicted_valid && stopped.total_samples()==predicted_stop,
              "protected stop did not match its nonmutating physical forecast");
        check(stopped.source_valid_payload_end()<=source_at+2*modem::pattern_pulse_padding_samples(c),
              "scheduled stop admitted a whole long symbol");
        const auto total=stopped.total_samples();
        const modem::SchedHopSourceMap stop_map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        const auto physical_origin=stopped.physical_output_origin();
        const auto source_origin=stop_map.locate(physical_origin).source.sample;
        const auto full_begin=stopped.source_valid_payload_end();
        const modem::FhssPowerShape fhss=c.fake_hopping?modem::FhssPowerShape(*c.fake_hopping,c.sample_rate,
            modem::pattern_pulse_padding_samples(c),modem::symbol_sample_count(c)):modem::FhssPowerShape{};
        const auto second=c.stream_epoch*c.sample_rate;
        const auto origin_offset=physical_origin>=second?static_cast<long double>(physical_origin-second):
            -static_cast<long double>(second-physical_origin);
        const auto full_end=stopped.source_total_samples()-2*modem::pattern_pulse_padding_samples(c)-1;
        std::uint64_t full_power=0;
        for(auto q=stopped.physical_valid_payload_end();q<total;++q) {
            const auto cell=stop_map.locate(physical_origin+q);
            if(cell.active && cell.source.sample-source_origin>=full_begin && cell.source.sample-source_origin<full_end &&
               modem::SchedHopSourceMap::gain(cell,{physical_origin+q})==1. &&
               (!fhss.enabled || fhss.gain(c.stream_epoch,origin_offset+q,c.sample_rate)==1.))++full_power;
        }
        check(full_power>=3*c.sample_rate,"scheduled protected cover counted nulls or ramps as useful masking");
        while(!stopped.finished())stopped.read_analytic(buffer);
        check(stopped.samples_emitted()==total && std::abs(buffer[(total-at-1)%buffer.size()])<1e-9,
              "scheduled stop failed to drain its shaped endpoint");
        modem::PatternTransmitter untouched(Bytes{0},c,c.stream_epoch);untouched.request_finish();
        check(untouched.finished() && untouched.total_samples()==0 && untouched.physical_valid_payload_end()==0,
              "stop before output manufactured private evidence");
        std::stop_source abort;abort.request_stop();bool rejected=false;
        try {ordinary.read_analytic(buffer,abort.get_token());}catch(const Error&){rejected=true;}
        check(rejected,"immediate scheduled abort was ignored");
    }
}
void exposure_identity() {
    for(unsigned mode:{0U,1U,2U})for(bool finish:{false,true})for(bool noise:{false,true}) {
        auto c=configuration(1,10,1,mode==2?6120:512);
        if(mode==0)c.scheduled_hopping.reset();
        if(mode==2) {
            c.integration_seconds=.125;
            auto plan=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);
            plan->clock_error_seconds=plan->peer_clock_error_seconds=.01;
            plan->region_seconds=.001;plan->audio_error_seconds=.05;c.scheduled_hopping=plan;
        }
        if(noise)c=modem::NoiseTransmitter::prepare_config(c);
        if(modem::scheduled_bursts(c))c=aligned(c);
        std::unique_ptr<modem::PatternTransmitter> message;
        std::unique_ptr<modem::NoiseTransmitter> tuning;
        if(noise)tuning=std::make_unique<modem::NoiseTransmitter>(c,modem::NoiseTransmitter::PreparedTemporaryKeys{},3);
        else message=std::make_unique<modem::PatternTransmitter>(Bytes{0,1,0},c,c.stream_epoch);
        const auto exercise=[&](auto& tx) {
            const auto reference=[&] {
                return detail::exposed_transmit_source_epoch(c,c.stream_epoch,tx.original_source_total_samples(),
                    tx.source_samples_emitted(),tx.source_valid_payload_end());
            };
            check(tx.emitted_payload_epoch()==reference(),"cached no-output exposure differs from independent static reference");
            const auto target=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c)+modem::symbol_sample_count(c)/2;
            std::array<Complex,113> block{};bool stopping=false;
            while(!tx.finished()) {
                const auto n=tx.read_analytic(block);check(n!=0,"exposure fixture stalled");
                check(tx.emitted_payload_epoch()==reference(),"cached source exposure changed pulse lookahead, nulls, cover or actual UTC identity");
                if(finish && !stopping && tx.source_samples_emitted()>=target) {
                    tx.request_finish();stopping=true;
                    check(tx.emitted_payload_epoch()==reference(),"partial stop extended cached exposure into independent cover");
                }
            }
            check(tx.emitted_payload_epoch()==reference(),"drained source lost its immutable exposure identity");
        };
        if(noise)exercise(*tuning);else exercise(*message);
    }
    // The public wrapper must forward the same retained source accounting.
    auto c=aligned(configuration(10));modem::StreamingTransmitter stream(modem::RawBits{Bytes{0,1}},c);
    std::array<float,113> pcm{};stream.read(pcm);
    check(stream.emitted_payload_epoch()==detail::exposed_transmit_source_epoch(c,c.stream_epoch,
        stream.original_source_total_samples(),stream.source_samples_emitted(),stream.source_valid_payload_end()),
        "streaming exposure wrapper changed the source accounting");
}
void disabled_and_short_guards() {
    auto plain=configuration(1,1,1);plain.data_key.reset();plain.scramble=plain.dsss=false;
    transfer::Options options;options.modem=plain;options.timestamp=plain.stream_epoch;
    auto source=transfer::binary_transmitter(Bytes{0},options);
    check(source!=nullptr,"disabled schedule pointer rejected ordinary unencrypted transmission");
    for(const unsigned factor:{1U,10U,100U,1000U})for(bool with_fhss:{false,true}) {
        auto c=configuration(factor,10,1,48000);c.integration_seconds=512./48000;
        if(with_fhss) {
            auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=2;hopping->dwell_seconds=.4;
            hopping->shifts[0]=30000000;hopping->shifts[1]=30100000;
            hopping->power_ramps=hopping->mandatory_ramps=hopping->smooth_ramps=true;
            hopping->clock_error_seconds=hopping->peer_clock_error_seconds=.01;
            hopping->audio_error_seconds=.05;hopping->region_seconds=.001;hopping->ramp_seconds=.01;
            c.fake_hopping=hopping;
        }
        c=aligned(c,false);
        check(modem::pattern_chips_per_symbol(c)>=64,"short-symbol fixture lost statistical pattern length");
        check(modem::packed_scheduled_symbols(c),"short fixture did not select protected full-power slots");
        const modem::ScheduledSymbolCalendar calendar(c);
        const modem::SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        const auto rank=calendar.first_rank(c.stream_epoch,c.stream_phase_samples);
        const auto count=calendar.slots_per_group()+2;
        Bytes bits(count);for(std::uint64_t i=0;i<count;++i)bits[i]=i%2;
        modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false),fragmented(bits,c,c.stream_epoch,0,false);
        const auto actual=capture(tx,509),pieces=capture(fragmented,31);
        check(actual.size()==pieces.size() && actual.size()==modem::pattern_transmission_sample_count(c,count,false),
              "packed short duration did not include all deferred bits");
        for(std::size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-pieces[i])<1e-9,"packed rendering depends on block size");
        const auto support=modem::pattern_pulse_padding_samples(c),symbol=modem::symbol_sample_count(c);
        const auto anchor=c.stream_epoch*c.sample_rate+c.stream_phase_samples;
        const auto first_source=map.locate(anchor).source.sample;
        const modem::FhssPowerShape shape=c.fake_hopping?modem::FhssPowerShape(*c.fake_hopping,c.sample_rate,support,symbol):modem::FhssPowerShape{};
        for(std::uint64_t i=0;i<count;++i) {
            const auto start=calendar.physical_slot(rank+i);
            const auto address=modem::symbol_stream_address(c,c.stream_epoch,c.stream_phase_samples,i);
            check(address.epoch==start/c.sample_rate && address.sample_in_second==start%c.sample_rate,
                  "packed private/Data pattern address reused virtual time");
            check(calendar.offset_source(c.stream_epoch,c.stream_phase_samples,i)==calendar.source_slot(rank+i)-first_source,
                  "packed source offset lost its immutable anchor");
            for(auto q=start-support;q<start+symbol+support;++q) {
                const auto cell=map.locate(q);
                check(cell.active && modem::SchedHopSourceMap::gain(cell,{q})==1.,"short shaped symbol crossed a scheduled ramp or null");
                check(!shape.enabled || shape.gain(q/c.sample_rate,q%c.sample_rate,c.sample_rate)==1.,
                      "short shaped symbol crossed an inner FHSS ramp");
            }
        }
        // Exact anchor reuse must invalidate on phase changes while keeping
        // fresh symbol identities across a future keyed active group.
        auto changed=c;const auto second=calendar.physical_slot(rank+1);
        check(second/c.sample_rate==c.stream_epoch,"phase-cache fixture crossed its first UTC second");
        changed.stream_phase_samples=second%c.sample_rate;
        modem::PatternCode reused(c,c.stream_epoch),fresh(changed,changed.stream_epoch);
        const auto far=calendar.slots_per_group()+2;
        (void)reused.value(far*reused.chips_per_symbol(),0);
        reused.set_stream_phase_samples(changed.stream_phase_samples);
        for(const auto index:std::array<std::uint64_t,3>{0,far,1})for(unsigned bit:{0U,1U})
            for(std::uint64_t chip=0;chip<reused.chips_per_symbol();++chip)
                check(reused.value(index*reused.chips_per_symbol()+chip,bit)==fresh.value(index*fresh.chips_per_symbol()+chip,bit),
                      "packed numeric anchor cache retained a stale phase/keyed symbol identity");
        check(calendar.physical_slot(rank+count-1)>anchor+count*symbol,"short calendar did not defer across a null group");
    }
    auto rectangular=configuration(1,10,1,48000);rectangular.integration_seconds=512./48000;rectangular.pulse_shaping=false;
    rectangular=aligned(rectangular,false);
    check(modem::packed_scheduled_symbols(rectangular) && modem::pattern_pulse_padding_samples(rectangular)==0,
          "rectangular packed fixture did not select the unshaped path");
    modem::PatternTransmitter rect(Bytes{0,1},rectangular,rectangular.stream_epoch);
    std::array<Complex,101> rect_buffer{};while(rect.source_samples_emitted()<=modem::training_sample_count(rectangular)+100)
        rect.read_analytic(rect_buffer);
    rect.request_finish();Complex last{};
    while(!rect.finished()) {const auto n=rect.read_analytic(rect_buffer);check(n!=0,"rectangular stop stalled");last=rect_buffer[n-1];}
    check(std::abs(last)<1e-12,"packed rectangular cancellation lost its shutdown fade");
    auto large=configuration(1,10000,1000,6120);
    const auto tail=modem::protected_cover_span_samples(large);
    check(tail<10ULL*large.sample_rate,"large active group imposed an unnecessary whole-group protected-stop wait");
    check(modem::scheduled_minimum_flat_fraction(large,tail)*tail>=3ULL*large.sample_rate,
          "shortened large-group cover did not certify three useful seconds");
    for(const auto fs:{511U,512U}) {
        auto fractional=configuration(1,10000,1000,fs);
        auto hops=std::make_shared<modem::FhssPlan>();hops->channels=2;hops->dwell_seconds=.4;
        hops->shifts[0]=30000000;hops->shifts[1]=30100000;
        hops->power_ramps=hops->mandatory_ramps=hops->smooth_ramps=true;
        hops->clock_error_seconds=hops->peer_clock_error_seconds=.01;
        hops->audio_error_seconds=.05;hops->ramp_seconds=.01;fractional.fake_hopping=hops;
        check(modem::protected_cover_span_samples(fractional)<10ULL*fs,
              "fractional-rate FHSS cover waited through an unnecessary large active group");
        auto group=std::make_shared<modem::SchedHopPlan>(*fractional.scheduled_hopping);group->group_hops=7;group->time_multiplier=10;
        fractional.scheduled_hopping=group;
        const modem::SchedHopSourceMap map(*group,*fractional.data_key,fs);
        const modem::FhssPowerShape shape(*hops,fs,modem::pattern_pulse_padding_samples(fractional),modem::symbol_sample_count(fractional));
        const auto B=map.source_samples_per_group();
        for(const auto start:std::array<std::uint64_t,4>{0,17,B-19,B+99})for(const auto length:std::array<std::uint64_t,3>{137,1550,3000}) {
            std::uint64_t actual=0;
            for(auto u=start;u<start+length;++u) {
                const auto q=map.source_to_physical({u});const auto where=map.locate(q);
                actual+=modem::SchedHopSourceMap::gain(where,q)==1. && shape.gain(q.sample/fs,q.sample%fs,fs)==1.;
            }
            const auto bound=modem::scheduled_minimum_flat_fraction(fractional,length)*length;
            check(bound<=actual+1e-8,"periodic fractional-rate cover certificate overstated usable samples");
        }
    }
    for(bool inner:{false,true}) {
        auto long_guard=configuration(1,10,1,6120);long_guard.integration_seconds=3600;
        auto slow_guard=std::make_shared<modem::SchedHopPlan>(*long_guard.scheduled_hopping);
        slow_guard->audio_error_seconds=.25;long_guard.scheduled_hopping=slow_guard;
        if(inner) {
            auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=2;hopping->dwell_seconds=.4;
            hopping->shifts[0]=30000000;hopping->shifts[1]=30100000;
            hopping->power_ramps=hopping->mandatory_ramps=hopping->smooth_ramps=true;
            hopping->clock_error_seconds=hopping->peer_clock_error_seconds=.01;
            hopping->audio_error_seconds=.25;hopping->ramp_seconds=.01;long_guard.fake_hopping=hopping;
        }
        check(modem::scheduled_minimum_flat_fraction(long_guard)<.5,"poor-duty long-symbol fixture lost its guard cost");
        check(!modem::packed_scheduled_symbols(long_guard),"poor guard duty demanded a whole hours-long symbol fit a hop");
    }
    // An impossible full-support geometry fails explicitly rather than emitting
    // a symbol whose entire evidence is suppressed by the physical envelope.
    auto impossible=configuration(1,10,1,48000);impossible.integration_seconds=.2;
    auto guards=std::make_shared<modem::SchedHopPlan>(*impossible.scheduled_hopping);
    guards->audio_error_seconds=.25;impossible.scheduled_hopping=guards;
    check(modem::packed_scheduled_symbols(impossible),"invalid slot fixture did not need deferral");
    bool rejected=false;try {modem::PatternTransmitter unsafe(Bytes{0,1},impossible,impossible.stream_epoch,0,false);}
    catch(const Error& e){rejected=std::string(e.what()).find("no complete shaped symbol fits")!=std::string::npos;}
    check(rejected,"impossible scheduled slot silently erased a short bit");

}
}
int main() {
    try {split_waveform();continuous_reference();temporary_noise_and_finish();exposure_identity();disabled_and_short_guards();std::cout<<"scheduled transmitter checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
