#include "datapump/fast/modem.hpp"
#include "datapump/fast/ldpc.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
using namespace datapump::fast;
using Bits=std::array<std::uint8_t,physical_interval_bits>;
static void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
namespace {
void independent_metrics() {
    const auto q=square_qam_constellation(16);
    require(std::abs(q[0]-std::complex<double>(-3,-3)/std::sqrt(10.))<1e-14,"16QAM independent zero vector");
    require(std::abs(q[15]-std::complex<double>(1,1)/std::sqrt(10.))<1e-14,"16QAM Gray middle vector");
    std::mt19937 rng(7651);std::uniform_real_distribution<double> uniform(-2,2);
    for(unsigned bits=2;bits<=22;bits+=2) {
        const auto order=1u<<bits,side=1u<<(bits/2);
        const auto points=square_qam_constellation(order);
        double energy=0;for(auto v:points)energy+=std::norm(v);
        require(std::abs(energy/order-1)<1e-11,"QAM unit energy");
        auto p=capacity_profile();p.constellation=order;p.rolloff=.2;p.symbol_rate=15000;p.amplitude=.35;
        Bits payload{};for(auto& bit:payload)bit=rng()&1;
        bool sent=false,mapper_ok=true;std::size_t symbols=0;
        Transmitter tx(p,[&](std::span<std::uint8_t> output) {
            if(sent)return false;std::copy(payload.begin(),payload.end(),output.begin());sent=true;return true;
        },[&](std::complex<float> actual) {
            unsigned label=0;
            for(unsigned bit=0;bit<bits;++bit) {
                const auto offset=symbols*bits+bit;
                label=(label<<1)|(offset<payload.size()?payload[offset]:0);
            }
            mapper_ok&=std::abs(std::complex<double>(actual)-points[label])<1e-6;++symbols;
        });
        Receiver rx(p,[](std::span<const float>){});
        require(tx.workspace_bytes()<2*1024*1024 && rx.workspace_bytes()<1024*1024,
            "analytic QAM modem allocated an order-sized constellation");
        std::array<float,512> pcm{};while(!tx.finished())tx.read(pcm);
        require(mapper_ok&&symbols==(physical_interval_bits+bits-1)/bits,"analytic Gray mapper differs from explicit lattice");
        const auto scale=std::sqrt(2.*(order-1)/3.);
        for(unsigned trial=0;trial<100;++trial) {
            const std::complex<double> value(uniform(rng),uniform(rng));
            std::vector<double> actual(bits);
            const auto label=square_qam_soft_demodulate(order,value,actual);
            unsigned expected_label=0;
            for(unsigned axis=0;axis<2;++axis) {
                std::vector<double> zero(bits/2,std::numeric_limits<double>::infinity()),one(zero);
                double best=std::numeric_limits<double>::infinity();unsigned nearest=0;
                for(unsigned level=0;level<side;++level) {
                    const auto gray=level^(level>>1);
                    const auto delta=(axis?value.imag():value.real())-(2.*level+1-side)/scale;
                    const auto distance=delta*delta;
                    if(distance<best){best=distance;nearest=gray;}
                    for(unsigned bit=0;bit<bits/2;++bit) {
                        auto& metric=(gray&(1u<<(bits/2-1-bit)))?one[bit]:zero[bit];
                        metric=std::min(metric,distance);
                    }
                }
                expected_label=(expected_label<<(bits/2))|nearest;
                for(unsigned bit=0;bit<bits/2;++bit)
                    require(std::abs(actual[axis*bits/2+bit]-(zero[bit]-one[bit]))<1e-12,"O(log M) QAM LLR differs from exhaustive reference");
            }
            require(label==expected_label,"QAM nearest label differs from exhaustive reference");
        }
    }
}
void invalid_geometry() {
    for(unsigned kind=0;kind<4;++kind) {
        auto p=capacity_profile();
        if(kind==0)p.rolloff=0;
        if(kind==1)p.marker_spacing_intervals=0;
        if(kind==2)p.pilot_spacing_symbols=0;
        if(kind==3)p.constellation=3;
        unsigned rejected=0;
        try{Transmitter tx(p,[](std::span<std::uint8_t>){return false;});}catch(const std::exception&){++rejected;}
        try{Receiver rx(p,[](std::span<const float>){});}catch(const std::exception&){++rejected;}
        try{(void)interval_symbols(p);}catch(const std::exception&){++rejected;}
        try{(void)total_interval_symbols(p,1);}catch(const std::exception&){++rejected;}
        try{(void)pulse_tail_symbols(p);}catch(const std::exception&){++rejected;}
        require(rejected==5,"invalid profile reached modem allocation or geometry division");
    }
    bool rejected=false;
    try{(void)total_interval_symbols(capacity_profile(),std::numeric_limits<std::size_t>::max());}
    catch(const std::exception&){rejected=true;}
    require(rejected,"symbol total overflow was not rejected");
}
void sampled(unsigned order,unsigned sample_rate,double rolloff=.2,double ppm=0,double echo=0,double noise=0,unsigned marker_period=4,double carrier_offset=0) {
    auto p=capacity_profile();p.constellation=order;p.sample_rate=sample_rate;p.amplitude=.35;
    p.marker_spacing_intervals=marker_period;
    p.rolloff=rolloff;p.symbol_rate=18000/(1+rolloff);
    require(interval_symbols(p,0)-interval_symbols(p,1)==sync_symbols,"sparse marker geometry");
    require(total_interval_symbols(p,37)==37*interval_symbols(p,1)+((36/marker_period)+1)*sync_symbols,"aggregate geometry");
    std::mt19937 rng(312);std::vector<Bits> input(ppm?40:12),output;
    const bool coded=order>=1048576;
    datapump::Bytes source,coded_bits;
    std::vector<float> received_soft;
    if(coded) {
        source.resize(ldpc::data_bits(p.code_rate)/8);
        for(auto& byte:source)byte=static_cast<std::uint8_t>(rng());
        coded_bits=ldpc::interleave(ldpc::encode(source,p.code_rate));
        input.resize((coded_bits.size()+physical_interval_bits-1)/physical_interval_bits);
    }
    for(auto& b:input)for(auto& bit:b)bit=rng()&1;
    if(coded)for(std::size_t i=0;i<input.size()*physical_interval_bits;++i)
        input[i/physical_interval_bits][i%physical_interval_bits]=i<coded_bits.size()?coded_bits[i]:0;
    // Central QAM points carry valid data even when their power is below the
    // legacy APSK's minimum-amplitude admission threshold.
    const auto bits=static_cast<unsigned>(std::log2(order));
    const auto center=(1u<<(bits/2-1)),gray=center^(center>>1),center_label=(gray<<(bits/2))|gray;
    for(std::size_t offset=0;!coded&&offset+bits<=input[0].size();offset+=bits)
        for(unsigned bit=0;bit<bits;++bit)input[0][offset+bit]=(center_label>>(bits-1-bit))&1;
    std::size_t sent=0;
    auto transmit_profile=p;transmit_profile.carrier_hz+=carrier_offset;
    Transmitter tx(transmit_profile,[&](std::span<std::uint8_t>b){if(sent==input.size())return false;std::copy(input[sent].begin(),input[sent].end(),b.begin());++sent;return true;});
    const auto tx_workspace=tx.workspace_bytes();
    std::vector<float> nominal(137,0),block(173);
    while(!tx.finished()){auto n=tx.read(block);nominal.insert(nominal.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(n));}
    require(tx.workspace_bytes()==tx_workspace,"QAM transmitter workspace grew");
    const auto expected_samples=static_cast<std::uint64_t>(std::floor(
        (preamble_symbols(p)+total_interval_symbols(p,input.size())+pulse_tail_symbols(p)-1)*
        p.sample_rate/p.symbol_rate))+1;
    require(tx.samples_generated()==expected_samples,"QAM preamble/tail sample geometry differs from helpers");
    auto pcm=nominal;
    if(ppm) {
        pcm.assign(static_cast<std::size_t>(nominal.size()*(1+ppm/1e6))+64,0);
        for(std::size_t i=20;i+20<pcm.size();++i) {
            const auto coordinate=static_cast<double>(i)/(1+ppm/1e6);
            const auto center_sample=static_cast<std::ptrdiff_t>(coordinate);
            double value=0;
            for(int j=-32;j<=32;++j) {
                const auto source=center_sample+j;
                if(source<0||source>=static_cast<std::ptrdiff_t>(nominal.size()))continue;
                const auto t=coordinate-static_cast<double>(source);
                const auto kernel=std::abs(t)<1e-9?1:std::sin(std::numbers::pi*t)/(std::numbers::pi*t)*(.42+.5*std::cos(std::numbers::pi*t/33)+.08*std::cos(2*std::numbers::pi*t/33));
                value+=nominal[static_cast<std::size_t>(source)]*kernel;
            }
            pcm[i]=static_cast<float>(value);
        }
    }
    std::normal_distribution<float> gaussian(0,static_cast<float>(noise));
    if(echo||noise)for(std::size_t i=pcm.size()-1;i>0;--i)pcm[i]+=static_cast<float>(echo)*pcm[i-1]+gaussian(rng);
    std::size_t erasures=0;
    Receiver rx(p,[&](std::span<const float>b){Bits got{};for(std::size_t i=0;i<b.size();++i){got[i]=b[i]>0;erasures+=b[i]==0;}output.push_back(got);if(coded)received_soft.insert(received_soft.end(),b.begin(),b.end());});
    const auto rx_workspace=rx.workspace_bytes();
    for(std::size_t i=0;i<pcm.size();i+=191)rx.push(std::span<const float>(pcm).subspan(i,std::min<std::size_t>(191,pcm.size()-i)));
    require(!rx.progress().physical_complete,"EOF must not complete capacity RX");
    block.assign(sample_rate*5,0);rx.push(block);
    require(!rx.progress().physical_complete,"capacity receiver completed before six seconds of absence");
    block.assign(sample_rate*2,0);rx.push(block);
    std::size_t errors=0;for(std::size_t i=0;i<std::min(input.size(),output.size());++i)for(std::size_t j=0;j<physical_interval_bits;++j)errors+=input[i][j]!=output[i][j];
    std::cout<<order<<"QAM "<<sample_rate<<" Hz alpha "<<rolloff<<" ppm "<<ppm<<" echo "<<echo<<" carrier "<<carrier_offset<<" noise "<<noise<<" intervals "<<output.size()<<" errors "<<errors<<" erasures "<<erasures<<" EVM "<<rx.progress().evm<<'\n';
    if(coded) {
        // Very dense constellations can have occasional uncoded errors after
        // fractional resampling. Verify the actual LDPC output, not a relaxed
        // raw-BER allowance or an ideal symbol/noise bypass.
        require(output.size()==input.size()&&!erasures,"dense coded PCM lost fixed interval positions");
        received_soft.resize(ldpc::coded_bits);
        const auto decoded=ldpc::decode(ldpc::deinterleave(received_soft),p.code_rate);
        require(decoded.converged&&decoded.bytes==source,"dense QAM sampled PCM failed LDPC recovery");
    } else require(output==input,"high-order QAM sampled PCM failed");
    require(rx.progress().physical_complete,"capacity absence did not complete");
    require(rx.progress().evm<.01,"absent pilots contaminated capacity EVM");
    require(rx.workspace_bytes()==rx_workspace,"QAM receiver workspace grew");
}
}
int main(){try{
    independent_metrics();
    invalid_geometry();
    for(auto order:{4096u,16384u,65536u})for(auto sample_rate:{48000u,44100u})sampled(order,sample_rate);
    sampled(1048576,44100,.02);
    sampled(1048576,44100,.02,100);
    sampled(1048576,44100,.02,-100);
    sampled(4194304,44100,.02);
    sampled(4194304,44100,.02,100);
    sampled(4194304,44100,.02,-100);
    sampled(65536,48000,.1);
    sampled(65536,48000,.02);
    sampled(65536,48000,.02,0,0,0,16);
    sampled(65536,48000,.02,0,0,.0002,16);
    sampled(65536,48000,.2,100);
    sampled(16384,48000,.2,-100);
    sampled(65536,48000,.2,0,0,0,4,.7);
    sampled(16384,48000,.2,0,.04);
    sampled(4096,48000,.2,0,0,.00015);
    {
        auto p=capacity_profile();p.constellation=65536;p.rolloff=.2;p.symbol_rate=15000;
        std::size_t admitted=0;Receiver rx(p,[&](std::span<const float>){++admitted;});
        std::mt19937 rng(673);std::normal_distribution<float> noise(0,.1f);
        std::vector<float> unrelated(p.sample_rate);
        for(std::size_t i=0;i<unrelated.size();++i)
            unrelated[i]=noise(rng)+static_cast<float>(.2*std::cos(2*std::numbers::pi*p.carrier_hz*i/p.sample_rate));
        rx.push(unrelated);rx.finish();
        require(!rx.progress().acquired&&!admitted,"capacity noise/tone acquired through dense nearest-point slicing");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
