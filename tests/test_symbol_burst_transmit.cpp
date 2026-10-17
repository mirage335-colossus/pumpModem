#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_burst.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/transfer.hpp"
#include "../src/transmit_timing.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace datapump;
namespace {
using Complex=std::complex<double>;
void check(bool value,const char* message) {if(!value)throw Error(message);}
transfer::Options configuration(bool encrypted,unsigned factor=1) {
    transfer::Options options;
    options.timestamp=1800000000;
    auto& config=options.modem;
    config.sample_rate=64;config.carrier_hz=16;config.bandwidth_hz=16./factor;
    config.integration_seconds=factor==1?(encrypted?3:1.5):40;config.dsss_factor=factor;
    config.pulse_shaping=encrypted; // Explicitly cover the independent P=0 render path.
    config.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    config.symbol_burst={1,3};
    if(encrypted)options.key.emplace(Bytes(32,0x67));
    auto prepared=transfer::seeded_config(options,options.timestamp);
    const modem::SymbolBurstPlan plan(prepared);
    const auto slot=plan.first_slot({options.timestamp,0});
    options.timestamp=slot.payload_begin.epoch;
    config.stream_phase_samples=slot.payload_begin.phase;
    return options;
}
std::vector<Complex> capture(modem::PatternTransmitter& tx,std::size_t block) {
    check(tx.total_samples()<2000000,"Burst fixture exceeded its bounded capture scope");
    std::vector<Complex> result(tx.total_samples());std::size_t at=0;
    while(!tx.finished()) {
        const auto n=tx.read_analytic(std::span(result).subspan(at,std::min(block,result.size()-at)));
        check(n!=0,"Burst transmitter stalled before its endpoint");at+=n;
    }
    return result;
}
void sparse_waveform() {
    for(const bool encrypted:{false,true})for(const unsigned factor:{1U,10U}) {
        if(!encrypted && factor!=1)continue;
        auto options=configuration(encrypted,factor);
        auto config=transfer::seeded_config(options,options.timestamp);
        const modem::SymbolBurstPlan plan(config);
        modem::SymbolBurstCursor cursor(plan,plan.bind_payload(config.stream_epoch,config.stream_phase_samples));
        const auto first=cursor.at(0);const Bytes bits{0,1,0};
        modem::PatternTransmitter tx(bits,config,config.stream_epoch,0,false),fragmented(bits,config,config.stream_epoch,0,false);
        const auto actual=capture(tx,211),pieces=capture(fragmented,13);
        check(actual.size()==pieces.size(),"Burst block rendering changed its extent");
        const auto padding=modem::pattern_pulse_padding_samples(config);
        check((padding!=0)==encrypted,"Burst fixture missed a shaped or P=0 path");
        const auto last=cursor.at(bits.size()-1);
        check(actual.size()==plan.distance(first.support_begin,last.support_end),"Burst full-support duration is wrong");
        check(actual.size()==modem::pattern_transmission_sample_count(config,bits.size(),false),"Burst count helper and transmitter disagree");
        cursor.checkpoint(first);
        std::array<modem::SymbolBurstSlot,3> slots;
        for(std::size_t bit=0;bit<bits.size();++bit)slots[bit]=cursor.at(bit);
        modem::PatternCode code(config,config.stream_epoch);
        const auto amplitude=std::sqrt(2*modem::nominal_signal_power)*modem::outer_dsss_transmit_gain(config);
        std::size_t quiet=0,lit=0;
        for(std::uint64_t i=0;i<actual.size();++i) {
            check(std::abs(actual[i]-pieces[i])<1e-9,"Burst waveform depends on output block boundaries");
            Complex base{};bool support=false;
            for(std::size_t bit=0;bit<bits.size();++bit) {
                const auto begin=plan.distance(first.support_begin,slots[bit].support_begin);
                const auto end=plan.distance(first.support_begin,slots[bit].support_end);
                support|=i>=begin && i<end;
                const auto payload=plan.distance(first.support_begin,slots[bit].payload_begin);
                if(i>=begin && i<end)base+=code.shaped_value(bit*code.chips_per_symbol(),bits[bit],static_cast<double>(i)-payload);
            }
            const auto oscillator=std::polar(1.,2*std::numbers::pi*config.carrier_hz*static_cast<double>(i)/config.sample_rate);
            const auto signal=amplitude*base;
            check(std::abs(actual[i]-oscillator*(padding?modem::pattern_limit_pcm(signal):signal))<2e-8,"Burst changed complete symbol shaping or carrier phase");
            if(!support) {check(actual[i]==Complex{},"Burst silence emitted cover or a ramp");++quiet;}
            lit+=std::abs(actual[i])>1e-10;
        }
        check(quiet && lit,"Burst fixture did not exercise both symbols and quiet intervals");
        check(tx.working_bytes()<=config.memory_limit,"Burst transmitter exceeded workspace");
    }
}
void addresses_and_content() {
    auto options=configuration(true);
    const auto config=transfer::seeded_config(options,options.timestamp);
    const modem::SymbolBurstPlan plan(config);
    modem::SymbolBurstCursor cursor(plan,plan.bind_payload(options.timestamp,config.stream_phase_samples));
    Bytes plain(80);for(std::size_t i=0;i<plain.size();++i)plain[i]=(i%3)==0;
    auto actual=plain;transfer::xor_binary_bits(actual,options);
    for(std::size_t i=0;i<plain.size();++i) {
        const auto payload=cursor.at(i).payload_begin;
        const auto ordinal=payload.phase/plan.symbol_samples();
        const auto mask=options.key->stream(StreamPurpose::Data,payload.epoch,ordinal/8,1);
        check(actual[i]==(plain[i]^((mask[0]>>(7-ordinal%8))&1)),"Burst Data mask used a logical or contiguous UTC address");
    }
    transfer::xor_binary_bits(actual,options);
    check(actual==plain,"Burst Data masking is not symmetric");
    Message message;message.kind=MessageKind::text;
    const std::string text="seventeen bytes plus more";
    message.data.assign(text.begin(),text.end());
    const auto wire=transfer::message_wire_bits(message,options);
    check(wire.size()%1216==0,"Burst changed fixed interval framing");
    transfer::StreamReceiver receiver(options,options.timestamp);
    transfer::Received result;
    for(std::size_t at=0;at<wire.size();) {
        const auto count=std::min<std::size_t>(17,wire.size()-at);
        modem::PatternBurst burst;burst.first_stream_symbol=at;burst.stream_phase_samples=options.modem.stream_phase_samples;
        burst.bits.assign(wire.begin()+static_cast<std::ptrdiff_t>(at),wire.begin()+static_cast<std::ptrdiff_t>(at+count));
        result=receiver.push(std::move(burst));at+=count;
        check(!result.stream_complete,"Burst transport interpreted source before physical completion");
    }
    modem::PatternBurst end;end.complete=true;end.first_stream_symbol=wire.size();end.stream_phase_samples=options.modem.stream_phase_samples;
    result=receiver.push(std::move(end));
    check(result.content_validated && result.content.message.data==message.data,"Burst actual-UTC interval MAC or source recovery failed");
    auto unscheduled=options;unscheduled.timestamp=0;unscheduled.modem.stream_phase_samples=0;
    check(transfer::estimate(message,unscheduled).wire_bits==wire.size(),"Burst message estimate rejected an unset origin");
}
void stopping_and_tune() {
    auto options=configuration(true);
    auto config=transfer::seeded_config(options,options.timestamp);
    const modem::SymbolBurstPlan plan(config);const auto first=plan.bind_payload(config.stream_epoch,config.stream_phase_samples);
    const auto training=modem::training_sample_count(config);
    const auto quiet=training+plan.distance(first.support_begin,first.support_end)+7;
    modem::PatternTransmitter tx(Bytes{0,1,0},config,config.stream_epoch);
    std::vector<Complex> emitted(quiet);
    check(tx.read_analytic(emitted)==emitted.size(),"Burst quiet-stop fixture could not reach silence");
    check(tx.emitted_payload_epoch()==first.payload_begin.epoch,"Burst quiet time exposed a future payload epoch");
    const auto valid_before=tx.predicted_physical_valid_payload_end(tx.samples_emitted());
    tx.request_finish();
    check(tx.physical_valid_payload_end()==valid_before,"Burst stop forecast changed its protected valid endpoint");
    check(tx.physical_valid_payload_end()<=quiet,"Burst stop in silence included a future payload");
    const auto remaining=tx.total_samples()-tx.samples_emitted();
    check(remaining<=modem::transmit_stop_sample_count(config),"Burst silence stop scheduled a future slot or excessive tail");
    std::array<Complex,137> block{};
    while(!tx.finished())check(tx.read_analytic(block)!=0,"Burst protected stop stalled");
    check(tx.emitted_payload_epoch()==first.payload_begin.epoch,"Burst stop cover exposed a future key epoch");
    auto tune_config=modem::NoiseTransmitter::prepare_config(config);
    const auto began=std::chrono::steady_clock::now();
    modem::NoiseTransmitter open(tune_config,modem::NoiseTransmitter::PreparedTemporaryKeys{},0);
    check(std::chrono::steady_clock::now()-began<std::chrono::seconds(2),"Open Burst Tune enumerated its huge future horizon");
    check(open.total_samples()==modem::NoiseTransmitter::sample_target,"Open Burst Tune lost its hard physical safety horizon");
    check(open.read_analytic(block)!=0,"Open Burst Tune could not produce its prefix");
    open.request_finish();
    while(!open.finished())check(open.read_analytic(block)!=0,"Open Burst Tune could not stop");
    modem::NoiseTransmitter finite(tune_config,modem::NoiseTransmitter::PreparedTemporaryKeys{},3);
    check(finite.total_samples()==modem::pattern_transmission_sample_count(tune_config,3),"Finite Burst Tune has an inexact endpoint");
}
void playback_grid() {
    auto options=configuration(true);
    double now=1800000001.25;
    const auto scheduled=detail::schedule_transfer_transmission(options,[](const auto& value) {
        const Bytes bits{0,1};return transfer::binary_transmitter(bits,value);
    },[&] {return now;},{},1800000031,.1);
    const auto config=transfer::seeded_config(options,options.timestamp);
    const modem::SymbolBurstPlan plan(config);
    const auto first=plan.bind_payload(scheduled.epoch,scheduled.phase_samples);
    check(first.support_begin.epoch%30==0 && first.support_begin.phase==0,"Burst initial support missed the UTC grid");
    check(scheduled.epoch>=1800000031,"Burst ignored a key epoch reservation");
    const auto expected=static_cast<double>(first.support_begin.epoch)-static_cast<double>(modem::training_sample_count(config))/config.sample_rate;
    check(std::abs(scheduled.playback_epoch-expected)<1e-8,"Burst prefix was scheduled at the payload boundary");
}
void unshaped_stop_boundaries() {
    for(const bool encrypted:{false,true}) {
        auto options=configuration(encrypted);options.modem.pulse_shaping=false;
        options.timestamp=1800000000;options.modem.stream_phase_samples=0;
        const auto config=transfer::seeded_config(options,options.timestamp);
        const modem::SymbolBurstPlan plan(config);const auto first=plan.bind_payload(config.stream_epoch,0);
        const auto second=plan.successor(first);
        const auto training=modem::training_sample_count(config);
        const auto boundary=training+plan.distance(first.payload_begin,second.payload_begin);
        auto ordinary=config;ordinary.symbol_burst={};
        modem::PatternTransmitter single(Bytes{0},config,config.stream_epoch),baseline(Bytes{0},ordinary,ordinary.stream_epoch);
        const auto burst_single=capture(single,137),ordinary_single=capture(baseline,137);
        check(burst_single.size()==ordinary_single.size(),"P=0 Burst changed the existing one-symbol extent");
        for(std::size_t i=0;i<burst_single.size();++i)
            check(std::abs(burst_single[i]-ordinary_single[i])<2e-8,"P=0 Burst changed the existing one-time prefix, symbol or suppression");
        for(const int delta:{-1,0,1,3,5}) {
            modem::PatternTransmitter tx(Bytes{0,1,0},config,config.stream_epoch);
            const auto cutoff=static_cast<std::uint64_t>(static_cast<std::int64_t>(boundary)+delta);
            std::vector<Complex> prefix(cutoff);
            check(tx.read_analytic(prefix)==prefix.size(),"P=0 stop fixture did not reach its cutoff");
            const auto forecast=tx.predicted_physical_valid_payload_end(cutoff);
            tx.request_finish();
            check(tx.physical_valid_payload_end()==forecast,"P=0 stop changed its valid payload forecast");
            check(forecast<=tx.total_samples(),"P=0 valid payload endpoint extends beyond stopped output");
            if(delta<=0)check(forecast==training+plan.symbol_samples(),"P=0 quiet or exact-boundary stop claimed an untouched symbol");
            std::array<Complex,131> block{};
            while(!tx.finished())check(tx.read_analytic(block)!=0,"P=0 protected stop stalled");
            check(tx.emitted_payload_epoch()==(delta>0?second.payload_begin.epoch:first.payload_begin.epoch),"P=0 stop exposed an untouched future key epoch or lost an emitted prefix");
        }
    }
}
void offline_origin_enumeration() {
    for(const bool encrypted:{false,true}) {
        auto options=configuration(encrypted);
        options.modem.sample_rate=65;options.modem.carrier_hz=16.25;
        auto config=transfer::seeded_config(options,options.timestamp);
        const modem::SymbolBurstPlan plan(config);
        const auto first=plan.first_slot({1800000000,0});
        // Use an unbound center and phase so the offline entry point must
        // derive the eligible UTC origin, including a nonzero private phase.
        options.timestamp=first.payload_begin.epoch+1;
        options.modem.stream_phase_samples=0;options.search_seconds=2;
        std::vector<std::uint64_t> visited;
        bool rejected_empty=false;
        try {
            (void)transfer::receive({},options,[&](auto epoch){visited.push_back(epoch);});
        } catch(const Error& error) {
            const std::string message=error.what();
            rejected_empty=message.find("no sufficiently confident pattern")!=std::string::npos;
            check(message.find("Symbol_Burst support must start")==std::string::npos,"Offline Burst receive bound an invalid payload phase");
        }
        check(rejected_empty,"Empty offline Burst input fabricated a reception");
        check(visited==std::vector<std::uint64_t>{first.payload_begin.epoch},"Offline Burst receive searched ineligible UTC payload epochs");
    }
}
void independent_fhss_calendar() {
    auto options=configuration(true);
    auto& c=options.modem;
    c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;c.integration_seconds=.25;
    c.symbol_burst={1,0};c.stream_phase_samples=0;
    auto hopping=std::make_shared<modem::FhssPlan>();
    hopping->channels=3;hopping->dwell_seconds=.4;
    hopping->shifts[0]=1000000;hopping->shifts[1]=1010000;hopping->shifts[2]=1033000;
    hopping->clock_error_seconds=hopping->peer_clock_error_seconds=.001;
    hopping->audio_error_seconds=.002;
    auto ordinary=c;ordinary.symbol_burst={};
    const auto original=modem::resolve_fhss_power(*hopping,ordinary);
    check(original->defer_symbols,"FHSS regression did not exercise existing short-symbol deferral");
    const auto resolved=modem::resolve_fhss_power(*hopping,c);
    check(resolved->power_ramps && resolved->smooth_ramps && !resolved->defer_symbols,
          "Production FHSS incorrectly imposed a dwell calendar on Symbol_Burst");
    check(resolved->ramp_seconds==original->ramp_seconds && resolved->dwell_seconds==original->dwell_seconds,
          "Symbol_Burst changed the independent FHSS ramps or dwell");
    c.fake_hopping=resolved;
    auto keyed=c;keyed.data_key=options.key;
    const modem::SymbolBurstPlan plan(keyed);
    const auto first=plan.first_slot({options.timestamp,0});
    options.timestamp=first.payload_begin.epoch;c.stream_phase_samples=first.payload_begin.phase;
    const auto config=transfer::seeded_config(options,options.timestamp);
    modem::PatternTransmitter tx(Bytes{0,1},config,config.stream_epoch,0,false);
    const auto actual=capture(tx,113);
    const auto first_end=plan.distance(first.support_begin,first.support_end);
    const auto next_begin=plan.distance(first.support_begin,plan.successor(first).support_begin);
    check(next_begin>first_end,"FHSS/Burst fixture contains no inter-symbol silence");
    for(auto sample=first_end;sample<next_begin;++sample)
        check(actual[sample]==Complex{},"FHSS composition filled the agreed Burst silence");
}
}
int main() {
    try {sparse_waveform();addresses_and_content();stopping_and_tune();playback_grid();unshaped_stop_boundaries();offline_origin_enumeration();independent_fhss_calendar();std::cout<<"Symbol_Burst transmit passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
