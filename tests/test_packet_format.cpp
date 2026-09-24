#include "datapump/packet.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class Function> void rejects(Function function,const char* message) {
    try { function(); } catch(const Error&) { return; }
    throw std::runtime_error(message);
}
Message content(std::size_t count) {
    Message message;message.id[0]=1;message.data.assign(count,'e');return message;
}
void compact_structure() {
    check(packet_prefix_size==14 && packet_min_prefix_size==4,"bootstrap constants describe the bounded maximum probe and minimum header");
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        PacketOptions options;options.fec=fec;
        for(const auto count:{0U,1U,15U,16U,80U,255U,256U,8192U}) {
            auto message=content(count);
            const auto wire=encode_packet(message,options);
            const auto layout=packet_layout(wire);
            const auto effective=count<16?FecMode::off:fec;
            check(layout.header_bytes>=4 && layout.header_bytes<=8,"variable header data length is bounded");
            check(layout.fec==effective && (wire[0]&3)==static_cast<unsigned>(effective),"controls begin at byte zero and tiny packets force FEC off");
            check(packet_header_extent(wire)==layout.header_bytes+layout.header_parity_bytes,"actual header extent is not the maximum probe size");
            if(count<16)check(layout.header_parity_bytes==0 && layout.body_parity_bytes==0,"tiny packet still carries Reed-Solomon parity");
            if(count<256)check(layout.header_bytes+layout.header_parity_bytes<14,"short payload still has a maximum-size bootstrap");
            check(decode_packet(wire).message.data==message.data,"length-selected compression roundtrip");
            if(count==80)check(layout.payload_bytes==30,"one static prefix code uses three bits for e, without a dictionary header");
            if(count==8192)check(layout.payload_bytes<128,"long payload should use high-ratio compression");
            auto damaged=wire;
            for(std::size_t position=0;position<layout.header_parity_bytes/2;++position)damaged[position]^=static_cast<std::uint8_t>(position+71);
            check(decode_packet(damaged).message.data==message.data,"corruption within the actual bootstrap parity budget remains correctable");
            for(std::size_t length=0;length<layout.header_bytes+layout.header_parity_bytes;++length)
                check(!packet_frame_size(Bytes(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(length))),"partial compact bootstrap must remain incomplete");
        }
    }
}
void expansion_and_incompressible() {
    const auto message=content(32768);const auto wire=encode_packet(message);
    rejects([&]{decode_packet(wire,{},4096);},"compressed original length must be bounded before allocation");
    Message random;random.id[0]=2;random.data.resize(8192);std::mt19937 source(371);
    for(auto& byte:random.data)byte=static_cast<std::uint8_t>(source());
    const auto layout=packet_layout(encode_packet(random));
    check(!layout.compressed && layout.payload_bytes==random.data.size(),"incompressible data must avoid expansion");
}
void length_boundaries_and_tiny_policy() {
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        PacketOptions options;options.fec=fec;options.compression=false;
        for(const auto count:{71U,72U,16326U,16327U}) {
            const auto message=content(count);const auto wire=encode_packet(message,options);
            const auto layout=packet_layout(wire);
            const std::size_t body=count+(count<128?56:57);
            const std::size_t header=body<128?4:body<16384?5:6;
            const auto ratio=fec==FecMode::rs20?1U:3U;
            const auto parity=fec==FecMode::off?0:(((header*ratio+4)/5+1)&~std::size_t{1});
            check(layout.body_bytes==body && layout.header_bytes==header && layout.header_parity_bytes==parity,
                  "canonical length boundary changed header size or same-rate header parity");
            check(wire.size()==header+parity+body+layout.body_parity_bytes,"wire includes unexplained padding bytes");
            check(decode_packet(wire).consumed_bytes==wire.size(),"decoder consumed an artificial tail");
        }
        for(const auto kind:{MessageKind::text,MessageKind::file,MessageKind::screenshot}) {
            for(const auto count:{0U,1U,15U,16U}) {
                auto message=content(count);message.kind=kind;message.filename=std::string(250,'f');
                message.callsign=std::string(64,'C');message.grid=std::string(32,'G');
                const auto wire=encode_packet(message,options);const auto layout=packet_layout(wire);
                check(layout.fec==(count<16?FecMode::off:fec),"tiny FEC policy depends on metadata size or message kind");
                if(count<16)check(layout.header_parity_bytes==0 && layout.body_parity_bytes==0,"tiny attachment retains parity");
                const auto decoded=decode_packet(wire).message;
                check(decoded.data==message.data && decoded.filename==message.filename && decoded.kind==kind,
                      "long metadata and tiny payload do not roundtrip");
                const auto baseline=packet_empty_layout(message,fec);
                check(baseline.fec==layout.fec && baseline.original_bytes==0 && baseline.payload_bytes==0,
                      "empty airtime baseline lost the original transmission's effective FEC");
                check(baseline.metadata_bytes==24+250+64+32 && baseline.body_bytes==baseline.metadata_bytes+32,
                      "empty baseline contains incremental payload or original-size overhead");
            }
        }
    }
}
}
int main() {
    try { compact_structure();expansion_and_incompressible();length_boundaries_and_tiny_policy();std::cout<<"compact packet format passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
