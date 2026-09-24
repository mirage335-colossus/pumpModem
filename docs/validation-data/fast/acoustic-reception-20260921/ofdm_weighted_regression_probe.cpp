#define main original_acoustic_main
#include "../../../../tests/test_fast_acoustic.cpp"
#undef main
int main(int argc,char** argv) {
    const double noise=argc>1?std::stod(argv[1]):.003;
    auto p=config();p.constellation=64;p.interleave_depth=4;
    Bytes source(30000);std::mt19937 rng(712);for(auto& b:source)b=rng();
    Crypto crypto(Bytes(32,0x4d));
    auto encoder=fast::testing::deterministic_encoder(p,crypto,byte_source(source),713);
    std::vector<Bytes> bits;
    for(;;){Bytes interval(physical_interval_bits);if(!encoder.next_interval(interval))break;bits.push_back(std::move(interval));}
    auto signal=transmit(p,bits);const auto block=p.ofdm_fft_size+p.ofdm_prefix_samples;
    for(std::size_t i=0;i<signal.size();++i) {
        const auto fraction=std::clamp((double(i)/block-2.)/12.,0.,1.);
        signal[i]*=static_cast<float>(.2+.8*fraction);
    }
    std::vector<float> pcm(1371,0);pcm.insert(pcm.end(),signal.begin(),signal.end());pcm.resize(pcm.size()+48000*8);
    std::mt19937 noise_rng(714);std::normal_distribution<float> gaussian(0,noise);
    for(std::size_t i=pcm.size();i-->0;)pcm[i]+=static_cast<float>(i>=1703?.45*pcm[i-1703]:0)+gaussian(noise_rng);
    StreamDecoder decoder(p,crypto);Receiver receiver(p,[&](std::span<const float> soft){decoder.push_interval(soft);});
    receiver.push(pcm);decoder.finish(receiver.progress().physical_complete);
    const auto d=decoder.snapshot();
    std::cout<<"noise "<<noise<<" acquired "<<receiver.progress().acquired<<" complete "<<d.complete<<" failedframes "<<d.ldpc_failed_frames<<" totalframes "<<d.ldpc_frames<<" iterations "<<d.ldpc_iterations<<" bytes "<<d.source_bytes<<" exact "<<(d.complete&&decoder.result()&&Bytes(decoder.result()->bytes().begin(),decoder.result()->bytes().end())==source)<<'\n';
}
