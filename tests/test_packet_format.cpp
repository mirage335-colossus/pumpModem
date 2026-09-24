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
    check(packet_prefix_size==16,"protected bootstrap must shrink from72 to16 bytes");
    for(const auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
        PacketOptions options;options.fec=fec;
        for(const auto count:{0U,1U,80U,255U,256U,8192U}) {
            auto message=content(count);
            const auto wire=encode_packet(message,options);
            const auto layout=packet_layout(wire);
            check(layout.header_bytes==8 && layout.header_parity_bytes==8,"compact bootstrap is RS(16,8)");
            check(wire[0]==0xd5 && (wire[1]&3)==static_cast<unsigned>(fec),"compact controls occupy two bytes");
            check(decode_packet(wire).message.data==message.data,"length-selected compression roundtrip");
            if(count==80)check(layout.payload_bytes==30,"one static prefix code uses three bits for e, without a dictionary header");
            if(count==8192)check(layout.payload_bytes<128,"long payload should use high-ratio compression");
            auto damaged=wire;
            for(const auto position:{0U,4U,9U,15U})damaged[position]^=static_cast<std::uint8_t>(position+71);
            check(decode_packet(damaged).message.data==message.data,"four damaged bootstrap bytes must remain correctable");
            damaged.resize(packet_prefix_size-1);
            check(!packet_frame_size(damaged),"partial compact bootstrap must remain incomplete");
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
}
int main() {
    try { compact_structure();expansion_and_incompressible();std::cout<<"compact packet format passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
