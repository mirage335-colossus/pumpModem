// Offline information-rate study. Does not modify or exercise receiver framing.
// Build against libdatapump_fast and libdatapump; see docs/fast-coding-study.md.
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr double ln2=0.6931471805599453094;
struct Moments {
    double sum=0,square=0;
    void add(double x) {sum+=x;square+=x*x;}
    double mean(unsigned n)const{return sum/n;}
    double se(unsigned n)const{return std::sqrt(std::max(0.,square/n-mean(n)*mean(n))/(n-1));}
};
double softplus(double x) {return std::max(x,0.)+std::log1p(std::exp(-std::abs(x)));}
std::vector<std::complex<double>> square_qam(unsigned m) {
    const unsigned side=static_cast<unsigned>(std::sqrt(m));
    std::vector<std::complex<double>> points(m);
    const double scale=std::sqrt(2.*(m-1)/3.);
    for(unsigned i=0;i<side;++i)for(unsigned j=0;j<side;++j)
        points[(i^(i>>1))*side+(j^(j>>1))]={(2.*i+1-side)/scale,(2.*j+1-side)/scale};
    return points;
}
}

int main(int argc,char** argv) {
    const unsigned samples=argc>1?static_cast<unsigned>(std::stoul(argv[1])):30000;
    if(samples<2)return 2;
    std::cout<<std::setprecision(10)
        <<"mapping,order,snr_in_band_db,es_n0_db,samples,cm_bits_per_symbol,cm_standard_error,bit_gmi,bit_gmi_standard_error,maxlog_gmi_grid,maxlog_scale,gaussian_bits_per_symbol,occupied_capacity_bps\n";
    for(const std::string kind:{"apsk","qam"})for(unsigned order:{4u,16u,64u,256u,1024u}) {
        if((kind=="apsk"&&order==1024)||(kind=="qam"&&order==4))continue;
        const auto points=kind=="apsk"?datapump::fast::constellation(order):square_qam(order);
        const unsigned bits=static_cast<unsigned>(std::log2(order));
        for(double snr_db:{4.,8.,10.,12.,14.,16.,18.,20.,22.,24.,26.,28.,30.,35.,40.}) {
            // SNR is power/noise in B=1.2*Rs, as in fast_regression.
            const double snr=std::pow(10.,snr_db/10),esn0=1.2*snr,n0=1/esn0;
            std::mt19937_64 rng(20260919+order);
            std::normal_distribution<double> normal(0,std::sqrt(n0/2));
            std::vector<double> distances(order);
            Moments cm,gmi;
            constexpr std::array<double,7> scales{.5,.625,.75,.875,1.,1.25,1.5};
            std::array<Moments,scales.size()> maxlog{};
            for(unsigned sample=0;sample<samples;++sample) {
                const unsigned label=static_cast<unsigned>(rng()%order);
                const auto y=points[label]+std::complex<double>(normal(rng),normal(rng));
                double minimum=std::numeric_limits<double>::infinity();
                for(unsigned k=0;k<order;++k)minimum=std::min(minimum,distances[k]=std::norm(y-points[k])/n0);
                double total=0;
                std::array<std::array<double,2>,10> sums{},mins;
                for(auto& pair:mins)pair.fill(std::numeric_limits<double>::infinity());
                for(unsigned k=0;k<order;++k) {
                    const double weight=std::exp(minimum-distances[k]);total+=weight;
                    for(unsigned b=0;b<bits;++b) {
                        const unsigned bit=(k>>b)&1;
                        sums[b][bit]+=weight;mins[b][bit]=std::min(mins[b][bit],distances[k]);
                    }
                }
                cm.add(bits-(distances[label]-minimum+std::log(total))/ln2);
                double loss=0;std::array<double,scales.size()> ml_loss{};
                for(unsigned b=0;b<bits;++b) {
                    const unsigned bit=(label>>b)&1;
                    loss+=(std::log(total)-std::log(std::max(sums[b][bit],1e-300)))/ln2;
                    const double signed_llr=mins[b][1-bit]-mins[b][bit];
                    for(unsigned s=0;s<scales.size();++s)ml_loss[s]+=softplus(-scales[s]*signed_llr)/ln2;
                }
                gmi.add(bits-loss);
                for(unsigned s=0;s<scales.size();++s)maxlog[s].add(bits-ml_loss[s]);
            }
            unsigned best=0;for(unsigned s=1;s<scales.size();++s)if(maxlog[s].sum>maxlog[best].sum)best=s;
            std::cout<<kind<<','<<order<<','<<snr_db<<','<<10*std::log10(esn0)<<','<<samples<<','
                <<cm.mean(samples)<<','<<cm.se(samples)<<','<<gmi.mean(samples)<<','<<gmi.se(samples)<<','
                <<maxlog[best].mean(samples)<<','<<scales[best]<<','<<std::log2(1+esn0)<<','
                <<18000*std::log2(1+snr)<<'\n'<<std::flush;
        }
    }
}
