#define main original_known_main
#include "acoustic_known_symbols.cpp"
#undef main
struct Moment {double x=0,y=0;C cross{};void add(C observed,C expected,double weight=1){x+=weight*std::norm(expected);y+=weight*std::norm(observed);cross+=weight*observed*std::conj(expected);}};
struct Fit {Moment train,valid,raw_train,raw_valid;};
struct Summary {std::size_t bins=0;double before=0,after=0,signal=0,raw_before=0,raw_after=0,raw_signal=0,bias=0;C correction{};};
int main(int argc,char** argv) {
    if(argc!=4)return 1;
    auto p=capacity_profile(Channel::acoustic);p.constellation=64;p.interleave_depth=8;p.code_rate=CodeRate::three_quarters;
    p.ofdm_fft_size=32768;p.ofdm_prefix_samples=4096;p.ofdm_pilot_stride=16;
    std::ifstream bf(argv[2],std::ios::binary);Bytes bits((std::istreambuf_iterator<char>(bf)),{});
    const auto cyclebits=cycle_intervals(p)*physical_interval_bits;
    std::map<std::pair<std::size_t,std::size_t>,Fit> bins;
    acoustic_ofdm::Receiver receiver(p,[](auto){},{},{},[&](const acoustic_ofdm::Observation& v){
        if(v.bit_offset>=bits.size()||!v.admitted)return;
        const auto available=std::min<std::size_t>(6,std::min<std::uint64_t>(cyclebits-v.bit_offset%cyclebits,bits.size()-v.bit_offset));
        const auto fill=mix(v.bin^mix(v.block+0x66696c6cULL));
        unsigned label=0;for(unsigned j=0;j<6;++j)label=(label<<1)|(j<available?bits[v.bit_offset+j]:((fill>>j)&1));
        const auto expected=point(64,label);auto& fit=bins[{v.bit_offset/cyclebits,v.bin}];
        const bool train=((v.block-16)%9)%2==0;
        (train?fit.train:fit.valid).add(v.value,expected);
        (train?fit.raw_train:fit.raw_valid).add(v.value,expected,v.channel_power);
    });
    std::ifstream audio(argv[1],std::ios::binary);std::array<float,2400> chunk;
    while(audio.read(reinterpret_cast<char*>(chunk.data()),sizeof(chunk))||audio.gcount())
        receiver.push(std::span(chunk).first(audio.gcount()/sizeof(float)));
    std::map<std::pair<std::size_t,int>,Summary> summary;
    for(auto& [key,f]:bins) {
        if(f.train.x<1e-12||f.valid.x<1e-12||f.raw_train.x<1e-12||f.raw_valid.x<1e-12)continue;
        const auto g=f.raw_train.cross/f.raw_train.x;
        const auto gt=f.train.cross/f.train.x,gv=f.valid.cross/f.valid.x;
        const auto before=f.valid.y+f.valid.x-2*f.valid.cross.real();
        const auto after=f.valid.y+std::norm(g)*f.valid.x-2*(std::conj(g)*f.valid.cross).real();
        const auto raw_before=f.raw_valid.y+f.raw_valid.x-2*f.raw_valid.cross.real();
        const auto raw_after=f.raw_valid.y+std::norm(g)*f.raw_valid.x-2*(std::conj(g)*f.raw_valid.cross).real();
        for(int band:{-1,static_cast<int>(key.second*48000./32768/250)}) {
            auto& s=summary[{key.first,band}];++s.bins;s.before+=before;s.after+=after;s.signal+=f.valid.x;
            s.raw_before+=raw_before;s.raw_after+=raw_after;s.raw_signal+=f.raw_valid.x;
            s.bias+=((gt-C{1})*std::conj(gv-C{1})).real();s.correction+=g;
        }
    }
    std::ofstream out(argv[3]);out.precision(12);
    out<<"cycle,band_low_hz,bins,normalized_mse_before,normalized_prediction_mse_after,raw_prediction_error_db_before,raw_prediction_error_db_after,crossvalidated_bias_power,mean_correction_real,mean_correction_imag\n";
    for(auto [key,s]:summary)out<<key.first<<','<<(key.second<0?-1:key.second*250)<<','<<s.bins<<','<<s.before/s.signal<<','<<s.after/s.signal<<','<<10*std::log10(s.raw_before/s.raw_signal)<<','<<10*std::log10(s.raw_after/s.raw_signal)<<','<<s.bias/s.bins<<','<<s.correction.real()/s.bins<<','<<s.correction.imag()/s.bins<<'\n';
}
