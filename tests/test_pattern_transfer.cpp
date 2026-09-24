#include "datapump/transfer.hpp"
#include "datapump/compression.hpp"
#include "datapump/tuning.hpp"
#include <iostream>
#include <cmath>
#include <array>

using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
transfer::Options options(bool keyed=false) {
    transfer::Options result;result.timestamp=1700000000;result.search_seconds=1;
    result.dsp_workspace_bytes=8*1024*1024;result.content_limit=1024*1024;
    if(keyed){std::array<std::uint8_t,32> seed{};seed[0]=91;result.key=Crypto(seed);}
    result.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,keyed).config;
    return result;
}
void exact_short_text() {
    Message message;message.data={'e'};
    for(bool keyed:{false,true}) {
        auto value=options(keyed);
        const auto bits=transfer::message_bits(message,value);
        check(bits.size()==3,"dictionary character must occupy exactly three meaningful bits");
        for(auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
            value.fec=fec;value.compression=false;
            PacketLayout layout;
            const auto estimate=transfer::estimate(message,value,&layout);
            const auto content_samples=3*modem::symbol_sample_count(value.modem);
            const auto hardware_samples=modem::training_sample_count(value.modem);
            check(estimate.waveform_samples==hardware_samples+content_samples,"short text adds only the rounded hardware prefix to its exact three-bit payload");
            check(std::abs(estimate.content_seconds-static_cast<double>(content_samples)/value.modem.sample_rate)<1e-12 &&
                  std::abs(estimate.total_seconds-estimate.content_seconds-static_cast<double>(hardware_samples)/value.modem.sample_rate)<1e-12,
                  "hardware settling airtime cannot be charged as meaningful content");
            check(layout.header_bytes==0 && layout.integrity_bytes==0,"short text inspection has no packet fields");
        }
        modem::ChannelConfig channel;channel.snr_db=6;channel.clock_error_ppm=100;channel.phase_noise_degrees_per_sqrt_second=.5;
        channel.receiver_timestamp=value.timestamp+1;
        const auto received=transfer::simulate(message,value,channel);
        check(received.raw_bits==bits,"blind PCM short-text reception preserves exact bits");
        check(received.packet.message.data==message.data,"dictionary interpretation uses detected exact endpoint");
        check(!received.packet_validated && !received.packet.authenticated,"pattern confidence does not claim packet validation or authentication");
    }
}
void raw_bits() {
    auto value=options();
    for(unsigned word=0;word<8;++word) {
        Bytes bits{static_cast<std::uint8_t>((word>>2)&1),static_cast<std::uint8_t>((word>>1)&1),static_cast<std::uint8_t>(word&1)};
        auto transmitter=transfer::binary_transmitter(bits,value);
        std::vector<float> pcm(137+static_cast<std::size_t>(transmitter->total_samples())+2*modem::symbol_sample_count(value.modem));
        std::size_t written=0;
        while(!transmitter->finished())written+=transmitter->read(std::span(pcm).subspan(137+written,std::min<std::size_t>(37,transmitter->total_samples()-written)));
        const auto received=transfer::receive(pcm,value);
        check(received.raw_bits==bits,"all eight three-bit messages decode without supplied bit count");
        if(word==1) {
            const auto payload_start=137+modem::training_sample_count(value.modem);
            const auto without_prefix=transfer::receive(std::span<const float>(pcm).subspan(payload_start),value);
            check(without_prefix.raw_bits==bits,"a receiver that misses all hardware settling still acquires the exact payload");
        }
    }
}
void packet_downstream() {
    auto value=options();value.fec=FecMode::off;Message message;message.data=Bytes(16,'e');
    const auto bits=transfer::message_bits(message,value);
    check(bits.size()%8==0 && bits.size()>128,"long content retains optional packet grammar");
    auto burst=modem::PatternBurst{};burst.bits=bits;burst.complete=true;
    auto decoded=transfer::interpret_pattern(burst,value,value.timestamp);
    check(decoded.packet_validated && decoded.packet.message.data==message.data,"packet parsing remains downstream of acquired bits");
    burst.bits.back()^=1;
    decoded=transfer::interpret_pattern(burst,value,value.timestamp);
    check(!decoded.packet_validated && decoded.raw_bits==burst.bits,"failed packet integrity cannot discard pattern-supported raw bits");
    value.content_limit=message.data.size();
    check(transfer::estimate(message,value).memory_supported,"encoded packet overhead uses its own workspace at the content limit");
    auto source=transfer::message_transmitter(message,value);
    check(source->total_samples()==modem::training_sample_count(value.modem)+bits.size()*modem::symbol_sample_count(value.modem),"packet bit count remains exact at the content limit with separate hardware settling");
    message.repeatable=true;value.repeat_policy.minimum_payload_bytes=0;value.repeat_policy.maximum_seconds=.001;
    bool rejected=false;
    try{(void)transfer::message_transmitter(message,value);}catch(const Error&){rejected=true;}
    check(rejected,"pattern transmission retains the sender's repeatable airtime policy");
}
void public_late_symbol_interpretation() {
    const auto value=options();Message text;text.data={'e'};
    modem::PatternBurst burst;burst.bits=transfer::message_bits(text,value);
    burst.first_stream_symbol=7;burst.score=45;burst.complete=true;
    const auto received=transfer::interpret_pattern(burst,value,value.timestamp);
    check(received.packet.message.data==text.data,"public patterns do not require an absolute secret stream index to interpret short text");
    check(received.diagnostics.pattern_score==burst.score,"a completed burst retains its own evidence score after receiver tracks drain");
}
void long_symbol_estimate() {
    auto value=options();value.modem.integration_seconds=3600;
    const auto estimate=transfer::estimate_binary(Bytes{0,0,1},value);
    check(modem::training_sample_count(value.modem)==0 && estimate.total_seconds==10800 &&
          estimate.content_seconds==estimate.total_seconds && estimate.memory_supported,
          "three hour-long symbols have no hardware prefix or retained waveform requirement");
}
}
int main(){try{exact_short_text();raw_bits();packet_downstream();public_late_symbol_interpretation();long_symbol_estimate();std::cout<<"pattern transfer tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
