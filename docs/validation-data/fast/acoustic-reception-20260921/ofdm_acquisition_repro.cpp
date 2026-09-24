#define main original_acoustic_main
#include "../../../../tests/test_fast_acoustic.cpp"
#undef main
int main() {
    auto p=config();
    std::mt19937 rng(711);
    std::vector<Bytes> bits(cycle_intervals(p),Bytes(physical_interval_bits));
    for(auto& interval:bits)for(auto& bit:interval)bit=rng()&1;
    const auto original=transmit(p,bits);const auto block=p.ofdm_fft_size+p.ofdm_prefix_samples;
    for(double gain:{1.,1.9,2.1,3.}) {
        auto pcm=original;
        for(std::size_t i=0;i<pcm.size();++i) {
            const auto progress=std::clamp((double(i)/block-3.)/11.,0.,1.);
            pcm[i]*=1.+(gain-1.)*progress;
        }
        pcm.insert(pcm.begin(),1371,0);pcm.resize(pcm.size()+48000*8);
        std::size_t delivered=0,wrong=0;Receiver receiver(p,[&](std::span<const float> llr) {
            for(std::size_t j=0;j<llr.size()&&delivered<bits.size();++j)wrong+=(llr[j]>0)!=bool(bits[delivered][j]);
            ++delivered;
        });
        receiver.push(pcm);
        std::cout<<"gain "<<gain<<" acquired "<<receiver.progress().acquired<<" complete "<<receiver.progress().physical_complete<<" intervals "<<delivered<<" wrong "<<wrong<<'\n';
    }
    for(std::size_t cut:{std::size_t{0},std::size_t{1000},std::size_t{4096},std::size_t{8192},std::size_t{12288}}) {
        std::vector<float> pcm(original.begin()+cut,original.end());pcm.resize(pcm.size()+48000*8);
        std::size_t delivered=0;Receiver receiver(p,[&](std::span<const float>){++delivered;});receiver.push(pcm);
        std::cout<<"cut "<<cut<<" acquired "<<receiver.progress().acquired<<" complete "<<receiver.progress().physical_complete<<" intervals "<<delivered<<'\n';
    }
}
