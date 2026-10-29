// Build against the bound historical helper_metadata.cpp/header, never the
// current metadata codec. This freezes test input; it is not production crypto.
#include "datapump/helper_metadata.hpp"
#include <array>
#include <cmath>
#include <iostream>

int main() {
    const datapump::Crypto key{std::array<std::uint8_t,32>{}};
    std::cout<<"{{\n";
    for(unsigned dwell=0;dwell<50;++dwell) {
        const auto ticks=static_cast<std::uint64_t>(std::round((1700000000.L+.4L*dwell)*65536));
        const auto packet=datapump::helper::timecode_packet(ticks,&key);
        if(packet.count!=3)return 1;
        std::cout<<"    {"<<packet.symbols[0]<<","<<packet.symbols[1]<<","<<packet.symbols[2]<<"}";
        std::cout<<(dwell==49?"\n":" ,\n");
    }
    std::cout<<"}}\n";
}
