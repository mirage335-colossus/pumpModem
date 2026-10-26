#include "datapump/transfer.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

using namespace datapump;
namespace {
void check(bool value,const char* text) {if(!value)throw Error(text);}
std::size_t diagnostics_bytes(const modem::Diagnostics& diagnostics) {
    return diagnostics.constellation.capacity()*sizeof(std::complex<double>)+
        diagnostics.waveform.capacity()*sizeof(float);
}
std::size_t pending_copy_bytes(const transfer::Received& result) {
    // Decoded application output and post-END recovery jobs belong to separate
    // pools. This is precisely the pre-END copy covered by the new query.
    const auto& message=result.content.message;
    return sizeof(result)+result.raw_bits.capacity()+diagnostics_bytes(result.diagnostics)+
        result.error.capacity()+message.filename.capacity()+message.callsign.capacity()+message.grid.capacity();
}
transfer::Received push_checked(transfer::StreamReceiver& receiver,modem::PatternBurst burst,
                                modem::Diagnostics diagnostics={}) {
    const auto actual=receiver.working_bytes()-receiver.source_buffer_bytes();
    const auto growth=receiver.retained_growth_bound(diagnostics_bytes(diagnostics));
    check(growth.has_value(),"supported retained-growth query is unavailable");
    auto result=receiver.push(std::move(burst),std::move(diagnostics));
    const auto retained=receiver.working_bytes()-receiver.source_buffer_bytes();
    const auto pending=result.stream_complete?0:pending_copy_bytes(result);
    check(retained+pending<=actual+*growth,
          "ordinary retained source/preview growth exceeded its query");
    return result;
}
transfer::Options options() {
    transfer::Options result;result.content_limit=128*1024;result.timestamp=1800000041;
    result.modem.stream_epoch=result.timestamp;result.compression=false;
    return result;
}
modem::PatternBurst piece(std::span<const std::uint8_t> bits,std::size_t offset,bool complete=false,
                         std::uint64_t identity=7) {
    modem::PatternBurst result;result.bits.assign(bits.begin(),bits.end());
    result.stream_first_sample=identity;result.stream_first_symbol=0;result.first_stream_symbol=offset;
    result.first_sample=identity+offset;result.end_sample=result.first_sample+bits.size();result.complete=complete;
    return result;
}
void framed_retention_and_shared_source() {
    auto configured=options();configured.recovery_options.enabled=false;
    Message source;source.data.resize(4096);
    for(std::size_t i=0;i<source.data.size();++i)source.data[i]=static_cast<std::uint8_t>(i*17+3);
    const auto wire=transfer::message_wire_bits(source,configured);
    constexpr auto cadence=boundary_sync::marker_bits+boundary_sync::interval_bits;
    check(wire.size()>12*cadence,"framed source does not exercise encoded growth");
    auto quota=std::make_shared<transfer::ReceiveStorageQuota>(
        transfer::ReceiveStorageQuota{transfer::source_storage_limit(configured.content_limit),0,0});
    transfer::StreamReceiver receiver(configured,configured.timestamp,quota);
    std::size_t at=0;
    for(unsigned frame=0;frame<6;++frame) {
        auto pending=push_checked(receiver,piece(std::span(wire).subspan(at,cadence),at));at+=cadence;
        check(!pending.stream_complete && !pending.content_validated && pending.raw_bits.size()<=4096,
              "framed pending source escaped physical completion");
    }
    const auto before=*receiver.retained_growth_bound(0),encoded=receiver.source_buffer_bytes();
    check(encoded && quota->reserved==encoded,"encoded capacity is not shared-quota accounted");
    for(unsigned frame=0;frame<6;++frame) {
        push_checked(receiver,piece(std::span(wire).subspan(at,cadence),at));at+=cadence;
    }
    check(receiver.source_buffer_bytes()>encoded && quota->reserved==receiver.source_buffer_bytes(),
          "framed source did not grow its independently accounted encoded buffer");
    check(*receiver.retained_growth_bound(0)==before,"query charged shared encoded growth a second time");
    while(at<wire.size()) {
        const auto count=std::min<std::size_t>(317,wire.size()-at);
        push_checked(receiver,piece(std::span(wire).subspan(at,count),at));at+=count;
    }
    auto complete=push_checked(receiver,piece({},at,true));
    check(complete.stream_complete && complete.content_validated && complete.content.message.data==source.data,
          "retained-growth query changed general framed source decoding");
    check(!quota->used && !quota->reserved && !receiver.source_buffer_bytes(),
          "physical END did not release shared encoded source retention");
}
void candidates_recovery_and_diagnostics() {
    auto configured=options();configured.recovery_options.retained_bits=1024;
    transfer::StreamReceiver receiver(configured,configured.timestamp);
    const Bytes prefix(1024,0);
    for(std::uint64_t identity=1;identity<=transfer::StreamReceiver::candidate_limit;++identity) {
        modem::Diagnostics diagnostics;diagnostics.constellation.resize(31);diagnostics.waveform.resize(37);
        auto result=push_checked(receiver,piece(prefix,0,false,identity),std::move(diagnostics));
        check(!result.stream_complete && result.raw_bits==prefix,"pending candidate prefix changed");
    }
    const Bytes one{1};
    auto over=push_checked(receiver,piece(one,0,false,999));
    check(!over.error.empty() && !over.stream_complete,"candidate quota became a physical END");
    // Crossing the configured packed hard-bit quota remains the unchanged
    // recovery-unavailable path; the ordinary pending source still advances.
    auto pending=push_checked(receiver,piece(one,1024,false,1));
    check(!pending.stream_complete && pending.observed_bits==1025,
          "retention query changed progress at the recovery quota");
}
void decoded_output_is_separate() {
    auto configured=options();configured.compression=true;
    Message source;source.data.assign(65536,'Q');
    const auto wire=transfer::message_wire_bits(source,configured);
    transfer::StreamReceiver receiver(configured,configured.timestamp);
    auto pending=push_checked(receiver,piece(wire,0));
    check(!pending.stream_complete && pending.content.message.data.empty(),"decompression preceded physical END");
    auto complete=push_checked(receiver,piece({},wire.size(),true));
    check(complete.stream_complete && complete.content_validated && complete.content.message.data==source.data,
          "query affected separately bounded post-END decompression");
}
void repeated_failures_and_calendar_shapes() {
    auto configured=options();configured.fec=FecMode::off;
    const std::array<std::uint8_t,32> master{};configured.key.emplace(master);
    Message source;source.data.assign(128,'X');
    auto wire=transfer::message_wire_bits(source,configured);
    constexpr auto cadence=boundary_sync::marker_bits+boundary_sync::interval_bits;
    check(wire.size()>=cadence,"error fixture requires a complete framed interval");
    wire[boundary_sync::marker_bits+17]^=1;
    transfer::StreamReceiver receiver(configured,configured.timestamp);
    auto failed=push_checked(receiver,piece(std::span(wire).first(cadence),0));
    check(!failed.stream_complete && failed.error=="Interval authentication failed",
          "fixed decoder failure did not remain pending");
    const Bytes one{1};
    auto reassigned=push_checked(receiver,piece(one,cadence+1));
    check(!reassigned.stream_complete && reassigned.error=="noncontiguous received source",
          "repeated fail did not exercise retained string assignment");
    const auto cached=*receiver.retained_growth_bound(0),working=receiver.working_bytes();
    check(receiver.retained_growth_bound(0)==cached && receiver.working_bytes()==working,
          "repeated query retained a prototype heap or changed its cached shape");
    // These are query-eligibility checks, not receiver/DSP qualification. Keep
    // actual FakeFCC/deferred and scheduled address error graphs eligible.
    configured.fec=FecMode::rs60; // keep the maximum packed recovery probe active
    auto fake=std::make_shared<modem::FhssPlan>();
    fake->channels=2;fake->shifts[1]=1000;fake->dwell_seconds=.4;
    fake->mandatory_ramps=fake->power_ramps=fake->smooth_ramps=fake->defer_symbols=true;
    fake->ramp_seconds=.01;
    configured.modem.fake_hopping=fake;
    configured.recovery_options.retained_bits=1024*1024;
    transfer::StreamReceiver deferred(configured,configured.timestamp);
    check(deferred.retained_growth_bound(0).has_value(),"deferred calendar query invented unknown error growth");
    auto scheduled=std::make_shared<modem::SchedHopPlan>();scheduled->time_multiplier=3;
    configured.modem.scheduled_hopping=scheduled;
    transfer::StreamReceiver calendar(configured,configured.timestamp);
    check(calendar.retained_growth_bound(0).has_value(),"scheduled calendar query invented unknown error growth");
    fake->power_fallback_reason.assign(513,'x');
    transfer::StreamReceiver oversized(configured,configured.timestamp);
    check(!oversized.retained_growth_bound(0),"unbounded configured diagnostic bypassed optional probe cutoff");
}
void unknown_and_overflow() {
    auto configured=options();transfer::StreamReceiver receiver(configured,configured.timestamp);
    const auto bytes=receiver.working_bytes();
    check(!receiver.retained_growth_bound(std::numeric_limits<std::size_t>::max()),
          "retained query accepted overflowing diagnostic capacity");
    check(receiver.working_bytes()==bytes,"read-only query retained a prototype allocation");
    configured.modem.symbol_burst.wait_symbols=1;
    transfer::StreamReceiver unsupported(configured,configured.timestamp);
    check(!unsupported.retained_growth_bound(0),"unknown cursor growth invented a retained bound");
}
}
int main() {try {
    framed_retention_and_shared_source();candidates_recovery_and_diagnostics();
    decoded_output_is_separate();repeated_failures_and_calendar_shapes();unknown_and_overflow();
    std::cout<<"retained growth tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
