// Offline exact-transmitted-bit OFDM diagnostics. Never opens audio devices.
#include "../src/fast/acoustic_ofdm.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/ldpc.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
using namespace datapump;
using namespace datapump::fast;
using C=std::complex<double>;
std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
C point(unsigned order,unsigned label) {
    unsigned axis=std::countr_zero(order)/2,side=1u<<axis,i=label>>axis,q=label&(side-1);
    for(unsigned shift=1;shift<axis;shift*=2){i^=i>>shift;q^=q>>shift;}
    const auto scale=std::sqrt(2.*(order-1)/3.);return {(2.*i+1-side)/scale,(2.*q+1-side)/scale};
}
struct Stats {
    std::size_t symbols=0,admitted=0,bits=0,errors=0;
    double signal=0,error=0,variance=0,channel=0,loss=0,clock=0,timing=0;
    C correlation{};
    void add(const acoustic_ofdm::Observation& v,C expected,std::span<const std::uint8_t> source) {
        ++symbols;admitted+=v.admitted;bits+=source.size();signal+=std::norm(expected);error+=std::norm(v.value-expected);
        correlation+=v.value*std::conj(expected);variance+=v.variance;channel+=v.channel_power;clock=v.clock_ppm;timing=v.timing_correction;
    }
};
struct BitStats {
    // The optional scale scan uses known transmitted bits after capture. It is
    // a confidence-calibration diagnostic, not an online decoding result.
    static constexpr std::array<double,9> scales{0.,.125,.25,.5,.75,1.,1.5,2.,4.};
    std::size_t bits=0,errors=0,erasures=0,confident_errors=0;
    double signed_llr=0;
    std::array<double,scales.size()> loss{};
    void add(double llr,bool bit) {
        ++bits;errors+=(llr>0)!=bit;erasures+=llr==0;
        const auto margin=bit?llr:-llr;signed_llr+=margin;
        confident_errors+=margin< -8;
        for(std::size_t i=0;i<scales.size();++i) {
            const auto x=-margin*scales[i];
            loss[i]+=std::max(0.,x)+std::log1p(std::exp(-std::abs(x)));
        }
    }
    void merge(const BitStats& other) {
        bits+=other.bits;errors+=other.errors;erasures+=other.erasures;
        confident_errors+=other.confident_errors;signed_llr+=other.signed_llr;
        for(std::size_t i=0;i<loss.size();++i)loss[i]+=other.loss[i];
    }
};
void save_bits(const std::string& path,const std::map<std::size_t,BitStats>& rows) {
    std::ofstream out(path);out.precision(12);
    out<<"index,compared_bits,errors,ber,erased_bits,confident_wrong_bits,mean_signed_llr,gmi_per_bit,oracle_scale,oracle_gmi_per_bit\n";
    for(const auto& [index,s]:rows) {
        const auto best=static_cast<std::size_t>(std::min_element(s.loss.begin(),s.loss.end())-s.loss.begin());
        out<<index<<','<<s.bits<<','<<s.errors<<','<<double(s.errors)/s.bits<<','<<s.erasures<<','<<s.confident_errors<<','
            <<s.signed_llr/s.bits<<','<<1-s.loss[5]/(s.bits*std::log(2.))<<','<<BitStats::scales[best]<<','
            <<1-s.loss[best]/(s.bits*std::log(2.))<<'\n';
    }
}
void save(const std::string& path,const std::map<std::size_t,Stats>& rows) {
    std::ofstream out(path);out.precision(12);
    out<<"index,symbols,admitted_symbols,compared_bits,errors,ber,evm,signal_error_db,mean_variance,mean_channel_power,phase,gain,gmi_per_symbol,clock_ppm,timing_samples\n";
    for(const auto& [index,s]:rows)out<<index<<','<<s.symbols<<','<<s.admitted<<','<<s.bits<<','<<s.errors<<','<<double(s.errors)/s.bits<<','<<std::sqrt(s.error/s.signal)
      <<','<<10*std::log10(s.signal/std::max(s.error,1e-30))<<','<<s.variance/s.symbols<<','<<s.channel/s.symbols<<','<<std::arg(s.correlation)
      <<','<<std::abs(s.correlation)/s.signal<<','<<(s.bits-s.loss/std::log(2.))/s.symbols<<','<<s.clock<<','<<s.timing<<'\n';
}
int main(int argc,char** argv){try{
    if(argc<4||argc>9)throw std::runtime_error("usage: acoustic_known_symbols capture.f32 tx.bits output-prefix [qam=64] [depth=4] [fft=8192] [cp=4096] [pilot-stride=8]");
    auto p=capacity_profile();p.channel=Channel::acoustic;p.acoustic_ofdm=true;p.code_rate=CodeRate::three_quarters;p.constellation=argc>4?std::stoul(argv[4]):64;
    p.interleave_depth=argc>5?std::stoul(argv[5]):4;p.ofdm_fft_size=argc>6?std::stoul(argv[6]):8192;p.ofdm_prefix_samples=argc>7?std::stoul(argv[7]):4096;
    p.ofdm_pilot_stride=argc>8?std::stoul(argv[8]):8;
    std::ifstream input(argv[2],std::ios::binary);if(!input)throw std::runtime_error("cannot open source bits");
    Bytes bits((std::istreambuf_iterator<char>(input)),{});
    const auto bps=std::countr_zero(p.constellation);
    const auto cycle_bits=cycle_intervals(p)*physical_interval_bits;
    std::vector<std::size_t> rotations(ldpc::coded_bits);
    for(std::size_t column=0;column<rotations.size();++column)
        rotations[column]=fast::testing::capacity_interleave_rotation(p,column);
    std::map<std::size_t,Stats> bins,blocks;std::size_t delivered=0;
    std::map<std::size_t,BitStats> cycles,frames,planes;
    acoustic_ofdm::Receiver rx(p,[&](std::span<const float>){++delivered;},{},{},[&](const acoustic_ofdm::Observation& v){
        if(v.bit_offset>=bits.size())return;
        const auto available=std::min<std::size_t>(bps,std::min<std::uint64_t>(cycle_bits-v.bit_offset%cycle_bits,bits.size()-v.bit_offset));
        auto source=std::span(bits).subspan(v.bit_offset,available);unsigned label=0;
        const auto fill=mix(v.bin^mix(v.block+0x66696c6cULL));
        for(unsigned j=0;j<bps;++j)label=(label<<1)|(j<available?source[j]:((fill>>j)&1));
        const auto expected=point(p.constellation,label);std::array<double,22> metrics{};
        square_qam_soft_demodulate(p.constellation,v.value,std::span(metrics).first(bps));
        std::size_t errors=0;double loss=0;
        for(std::size_t j=0;j<available;++j) {
            const auto llr=v.admitted?std::clamp(metrics[j]/std::max(v.variance,1e-8),-24.,24.):0.;
            errors+=(llr>0)!=bool(source[j]);const auto x=source[j]?-llr:llr;loss+=std::max(0.,x)+std::log1p(std::exp(-std::abs(x)));
            const auto position=(v.bit_offset+j)%cycle_bits;
            // Fixed-cycle alignment bits are not LDPC coordinates. Do not let
            // their known fill improve the reported decoder information margin.
            if(position<p.interleave_depth*ldpc::coded_bits) {
                const auto cycle=(v.bit_offset+j)/cycle_bits,column=position/p.interleave_depth;
                const auto frame=(position%p.interleave_depth+p.interleave_depth-rotations[column])%p.interleave_depth;
                BitStats contribution;contribution.add(llr,source[j]);
                cycles[cycle].merge(contribution);frames[cycle*p.interleave_depth+frame].merge(contribution);
                planes[j].merge(contribution);
            }
        }
        for(auto* s:{&bins[v.bin],&blocks[v.block]}){s->add(v,expected,source);s->errors+=errors;s->loss+=loss;}
    });
    std::ifstream pcm(argv[1],std::ios::binary);if(!pcm)throw std::runtime_error("cannot open capture");
    std::array<float,2400> chunk{};
    while(pcm.read(reinterpret_cast<char*>(chunk.data()),sizeof(chunk))||pcm.gcount()) {
        if(pcm.gcount()%sizeof(float))throw std::runtime_error("partial float sample");
        rx.push(std::span(chunk).first(static_cast<std::size_t>(pcm.gcount())/sizeof(float)));
    }
    rx.finish();save(std::string(argv[3])+"-bins.csv",bins);save(std::string(argv[3])+"-blocks.csv",blocks);
    save_bits(std::string(argv[3])+"-cycles.csv",cycles);save_bits(std::string(argv[3])+"-frames.csv",frames);
    save_bits(std::string(argv[3])+"-planes.csv",planes);
    std::cout<<"{\"acquired\":"<<rx.progress().acquired<<",\"physical_complete\":"<<rx.progress().physical_complete<<",\"intervals\":"<<delivered<<",\"observed_blocks\":"<<blocks.size()<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
