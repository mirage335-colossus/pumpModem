// Optional interoperability runner. Supply a separately obtained FLDigi source
// checkout; its implementation is deliberately not included in this repository.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include "pj_mfsk.h"
#include "datapump/legacy/modem.hpp"

int main(int argc,char** argv) {
    using namespace datapump::legacy;
    if(argc!=2) {std::fprintf(stderr,"usage: olivia-reference output.s16le\n");return 2;}
    const Config config{Mode::olivia4_2000,1500};
    const std::string message="CQ CQ TEST 123 Hello world\n";
    MFSK_Encoder encoder;encoder.BitsPerSymbol=2;encoder.Preset();
    unsigned char characters[2]={'C','Q'};encoder.EncodeBlock(characters);
    for(auto i=0;i<64;++i) std::printf("%u",unsigned(encoder.OutputBlock[i]^(encoder.OutputBlock[i]>>1)));
    std::puts("");
    characters[0]=characters[1]=0;encoder.EncodeBlock(characters);
    for(auto i=0;i<64;++i) std::printf("%u",unsigned(encoder.OutputBlock[i]^(encoder.OutputBlock[i]>>1)));
    std::puts("");

    MFSK_Receiver<double> reference_rx;
    reference_rx.Tones=4;reference_rx.Bandwidth=2000;
    reference_rx.FirstCarrierMultiplier=1.25;reference_rx.SyncIntegLen=4;
    reference_rx.SyncThreshold=8;reference_rx.Preset();
    auto transmitter=detail::olivia_transmitter(config,message,{});
    std::array<float,1024> pcm{};
    std::array<double,1024> converted{};
    std::string reference_text;
    const auto drain=[&]{unsigned char c;while(reference_rx.GetChar(c)>0)if(c>7)reference_text+=char(c);};
    while(auto count=transmitter->read(pcm)) {
        std::copy_n(pcm.begin(),count,converted.begin());
        reference_rx.Process(converted.data(),count);drain();
    }
    converted.fill(0);
    for(int i=0;i<20;++i) {reference_rx.Process(converted.data(),converted.size());drain();}
    reference_rx.Flush();drain();

    std::string product_text;
    auto receiver=detail::olivia_receiver(config,[&](auto text){product_text+=text;});
    MFSK_Transmitter<double> reference_tx;
    reference_tx.Default();reference_tx.Tones=4;reference_tx.Bandwidth=2000;
    reference_tx.FirstCarrierMultiplier=1.25;reference_tx.Preset();
    for(int i=0;i<16;++i) reference_tx.PutChar(0);
    for(unsigned char c:message) reference_tx.PutChar(c);
    for(int i=0;i<16;++i) reference_tx.PutChar(0);
    std::srand(1);reference_tx.Start();reference_tx.Stop();
    FILE* output=std::fopen(argv[1],"wb");
    if(!output)return 2;
    while(reference_tx.Running()) {
        const auto count=reference_tx.Output(converted.data());
        for(int i=0;i<count;++i) {
            pcm[static_cast<std::size_t>(i)]=static_cast<float>(converted[static_cast<std::size_t>(i)]);
            const auto value=static_cast<int>(std::nearbyint(std::clamp(double(pcm[static_cast<std::size_t>(i)]),-1.,1.)*32760));
            const unsigned bits=static_cast<unsigned>(value)&65535U;
            const unsigned char bytes[2]={static_cast<unsigned char>(bits&255U),static_cast<unsigned char>(bits>>8)};
            std::fwrite(bytes,1,2,output);
        }
        receiver->push(std::span<const float>(pcm).first(static_cast<std::size_t>(count)));
    }
    std::fclose(output);pcm.fill(0);
    for(int i=0;i<20;++i) receiver->push(pcm);
    std::printf("DataPump -> FLDigi: %s\nFLDigi -> DataPump: %s\n",reference_text.c_str(),product_text.c_str());
    return reference_text==message && product_text==message ? 0:1;
}
