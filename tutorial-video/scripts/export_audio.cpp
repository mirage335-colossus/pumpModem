// Build this standalone helper against an existing, matching Data Pump library.
// Exported audio is a separate fixed-epoch waveform, not the GUI trial's audio.
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    using namespace datapump;
    try {
        if(argc!=3)throw Error("Usage: export_audio KEYFILE EXTERNAL_OUTPUT.wav");
        auto root=std::filesystem::weakly_canonical(std::filesystem::absolute(__FILE__).parent_path().parent_path().parent_path());
        auto out=std::filesystem::weakly_canonical(argv[2]);
        if(std::mismatch(root.begin(),root.end(),out.begin(),out.end()).first==root.end()||std::filesystem::exists(out))
            throw Error("Choose a new output outside the repository");
        transfer::Options tx;tx.modem=tuning::resolve(3600,6.326999095983183,tuning::PatternMode::auto_keystream,true,1500).config;
        tx.timestamp=1800000000;tx.search_seconds=6;tx.dsp_workspace_bytes=512ULL*1024*1024;
        auto keys=load_keyring(argv[1]);auto key=std::find_if(keys.begin(),keys.end(),[](auto& k){return k.name=="Tutorial";});
        if(key==keys.end())throw Error("Tutorial demo key missing");tx.key=key->key;
        std::array<std::uint8_t,1> bits{1};auto source=transfer::binary_transmitter(bits,tx);
        if(source->total_samples()>25000000)throw Error("Waveform exceeds tutorial bound");
        std::vector<float> pcm(source->total_samples());
        for(std::size_t pos=0;pos<pcm.size();) {
            auto n=source->read(std::span(pcm).subspan(pos,std::min<std::size_t>(4096,pcm.size()-pos)));
            if(!n)throw Error("Waveform ended early");pos+=n;
        }
        std::ofstream f(out,std::ios::binary);modem::write_wav(f,pcm,tx.modem.sample_rate);
        if(!f)throw Error("WAV write failed");
        std::cout<<"Separate keyed bit1 waveform: rate3600Hz,carrier1500Hz,target6.326999095983183,sample_rate="<<tx.modem.sample_rate<<",seconds="<<double(pcm.size())/tx.modem.sample_rate<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
