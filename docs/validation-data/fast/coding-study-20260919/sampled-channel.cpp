#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace datapump::fast;
using Bits=std::array<std::uint8_t,physical_interval_bits>;
struct Result {
    ModemProgress progress;
    std::vector<std::array<float,physical_interval_bits>> soft;
    std::size_t wrong=0,known=0,zeros=0,misaligned=0,identifiable=0;
};
std::vector<float> wave(Profile p,const std::vector<Bits>& input) {
    std::size_t index=0;
    Transmitter tx(p,[&](std::span<std::uint8_t> out) {
        if(index==input.size())return false;
        std::copy(input[index].begin(),input[index].end(),out.begin());++index;return true;
    });
    std::vector<float> pcm;std::array<float,4096> chunk{};
    while(!tx.finished()) {
        auto n=tx.read(chunk);pcm.insert(pcm.end(),chunk.begin(),chunk.begin()+n);
    }
    return pcm;
}
Result run(Profile p,const std::vector<float>& clean,const std::vector<Bits>& input,double variance,std::uint64_t seed) {
    Result out;
    Receiver rx(p,[&](std::span<const float> soft) {
        out.soft.emplace_back();std::copy(soft.begin(),soft.end(),out.soft.back().begin());
    });
    std::mt19937_64 rng(seed);std::normal_distribution<float> noise(0,std::sqrt(variance));
    std::array<float,509> chunk{};
    rx.push(std::span<const float>(chunk).first(137));
    for(std::size_t offset=0;offset<clean.size();offset+=chunk.size()) {
        auto n=std::min(chunk.size(),clean.size()-offset);
        for(std::size_t i=0;i<n;++i)chunk[i]=clean[offset+i]+(variance?noise(rng):0.f);
        rx.push(std::span<const float>(chunk).first(n));
    }
    for(std::size_t remaining=7*p.sample_rate;remaining;) {
        auto n=std::min(remaining,chunk.size());
        for(std::size_t i=0;i<n;++i)chunk[i]=variance?noise(rng):0.f;
        rx.push(std::span<const float>(chunk).first(n));remaining-=n;
    }
    rx.finish();out.progress=rx.progress();
    for(std::size_t j=0;j<out.soft.size();++j) {
        std::size_t nonzero=0;for(float x:out.soft[j])nonzero+=x!=0;
        out.known+=nonzero;out.zeros+=physical_interval_bits-nonzero;
        if(j<input.size())for(std::size_t b=0;b<physical_interval_bits;++b)
            out.wrong+=out.soft[j][b]!=0 && (out.soft[j][b]>0)!=input[j][b];
        if(nonzero<128)continue;
        std::size_t best=input.size(),minimum=physical_interval_bits;
        for(std::size_t k=0;k<input.size();++k) {
            std::size_t wrong=0;
            for(std::size_t b=0;b<physical_interval_bits;++b)
                wrong+=out.soft[j][b]!=0 && (out.soft[j][b]>0)!=input[k][b];
            if(wrong<minimum){minimum=wrong;best=k;}
        }
        // Random interval identities are accepted only when agreement beats
        // an unrelated 50/50 sequence by a substantial margin.
        if(minimum*10>=nonzero*4){++out.misaligned;continue;}
        ++out.identifiable;out.misaligned+=best!=j;
    }
    return out;
}
std::pair<double,double> rate(const Result& result,const std::vector<Bits>& input,unsigned m) {
    // One global positive scale: this measures exactly the current bit metric,
    // including soft-zero erasures, rather than replacing the demapper.
    std::vector<double> signed_llr;signed_llr.reserve(input.size()*physical_interval_bits);
    for(std::size_t j=0;j<input.size();++j)for(std::size_t b=0;b<physical_interval_bits;++b)
        signed_llr.push_back(result.soft[j][b]*(input[j][b]?1.:-1.));
    auto loss=[&](double scale) {
        double sum=0;for(double llr:signed_llr) {
            const double z=-scale*llr;
            sum+=(std::max(0.,z)+std::log1p(std::exp(-std::abs(z))))/std::log(2.);
        }
        return sum/signed_llr.size();
    };
    // Convex one-dimensional cross-entropy; include zero scale (zero rate).
    double best_loss=1,best_scale=0;
    for(int i=0;i<=40;++i) {
        const double s=std::pow(10.,-3.+i*.125),v=loss(s);
        if(v<best_loss){best_loss=v;best_scale=s;}
    }
    return {m*(1-best_loss),best_scale};
}
int main(int argc,char**argv) {
    const std::size_t count=argc>1?std::stoul(argv[1]):64;
    const std::string selected=argc>2?argv[2]:"all";
    const auto started=std::chrono::steady_clock::now();
    std::mt19937 rng(5719);std::vector<Bits> input(count);
    for(auto& interval:input)for(auto& bit:interval)bit=rng()&1;
    std::cout<<std::setprecision(10)<<"profile,apsk,channel_model,snr_db,seed,tx_intervals,rx_intervals,acquired,physical_end,clean_alignment_valid,alignment_valid,identity_intervals,misaligned_intervals,known_hard_ber,erasure_fraction,evm,bit_metric_bits_per_symbol,normalized_bit_metric_rate,best_llr_scale,current_framed_metric_bps,received_signal_power,fullband_noise_variance,wall_seconds\n";
    for(auto channel:{Channel::wire,Channel::ssb,Channel::acoustic}) {
        if(selected!="all"&&selected!=channel_name(channel))continue;
        auto p=profile(channel);
        for(auto order:{4u,16u,64u,256u}) {
            p.constellation=order;auto clean=wave(p,input);const unsigned m=std::log2(order);
            const auto baseline=run(p,clean,input,0,417);
            const bool baseline_valid=baseline.soft.size()==count && baseline.wrong==0 && baseline.zeros==0 && baseline.misaligned==0;
            if(!baseline_valid) {std::cerr<<"Clean baseline failed: "<<channel_name(channel)<<" "<<order<<"\n";return 2;}
            for(unsigned model=0;model<(channel==Channel::acoustic?2u:1u);++model) {
                auto filtered=clean;
                if(model) {
                    filtered.resize(clean.size()+432);
                    for(std::size_t i=0;i<filtered.size();++i) {
                        double value=i<clean.size()?clean[i]:0;
                        if(i>=216&&i-216<clean.size())value+=.6*clean[i-216];
                        if(i>=432&&i-432<clean.size())value+=.2*clean[i-432];
                        filtered[i]=.6*value;
                    }
                }
                double power=0;for(float x:filtered)power+=double(x)*x;power/=filtered.size();
                for(double snr:{20.,30.}) {
                    const auto begin=std::chrono::steady_clock::now();
                    const double variance=power*std::pow(10.,-snr/10)*p.sample_rate/(2*p.symbol_rate*(1+p.rolloff));
                    auto result=run(p,filtered,input,variance,417);
                    bool valid=result.soft.size()==count && result.misaligned==0 && result.identifiable>0;
                    double bits=std::numeric_limits<double>::quiet_NaN(),scale=bits;
                    if(valid) {auto measured=rate(result,input,m);bits=measured.first;scale=measured.second;}
                    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
                    std::cout<<channel_name(channel)<<','<<order<<','<<(model?"echo216":"awgn")<<','<<snr<<",417,"<<count<<','<<result.soft.size()<<','<<result.progress.acquired<<','<<result.progress.physical_complete<<','<<baseline_valid<<','<<valid<<','<<result.identifiable<<','<<result.misaligned<<','<<double(result.wrong)/std::max<std::size_t>(1,result.known)<<','<<double(result.zeros)/(std::max<std::size_t>(1,result.soft.size())*physical_interval_bits)<<','<<result.progress.evm<<','<<bits<<','<<bits/m<<','<<scale<<','<<(bits/m)*physical_interval_bits*p.symbol_rate/interval_symbols(p)<<','<<power<<','<<variance<<','<<seconds<<std::endl;
                    std::cerr<<channel_name(channel)<<" M="<<order<<" model="<<model<<" snr="<<snr<<" valid="<<valid<<" metric="<<bits<<" elapsed="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<"\n";
                }
            }
        }
    }
}
