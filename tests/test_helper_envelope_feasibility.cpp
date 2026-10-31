// Standalone optimistic feasibility study of the actual Helper FHSS boundary
// ramps. This is NOT a receiver or a calibrated confidence interval. Each
// boundary is fitted independently, with an unknown complex gain for each
// pilot and the auxiliary on either side. No auxiliary symbols are decoded.
#include "datapump/helper_carrier.hpp"
#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using C = std::complex<double>;
namespace h = datapump::helper;
constexpr double fs=24000, pi=std::numbers::pi, tau=2*pi;
constexpr double true_audio=1.00005, true_arrival=.00321;
constexpr unsigned samples=672; // -12 ms through +16 ms at 24 kHz
constexpr double first=-.012, step=.0001;
constexpr unsigned candidates=61; // receiver search 0..6 ms, 0.1 ms grid
constexpr std::array<double,3> pilots{2400,550,950};

double gate(double t,double width) {
    const double x=std::clamp(t/width,0.,1.);
    return x*x*x*(x*(6*x-15)+10);
}

// Returns b^H G^-1 b for complex least squares or zero on numerical failure.
double projection(std::array<std::array<C,3>,3> g,std::array<C,3> b) {
    const auto original=b;
    for(unsigned j=0;j<3;++j) {
        unsigned pivot=j;
        for(unsigned k=j+1;k<3;++k)if(std::abs(g[k][j])>std::abs(g[pivot][j]))pivot=k;
        if(std::abs(g[pivot][j])<1e-10)return 0;
        std::swap(g[j],g[pivot]);std::swap(b[j],b[pivot]);
        const auto d=g[j][j];
        for(unsigned k=j;k<3;++k)g[j][k]/=d;
        b[j]/=d;
        for(unsigned i=0;i<3;++i)if(i!=j) {
            const auto f=g[i][j];
            for(unsigned k=j;k<3;++k)g[i][k]-=f*g[j][k];
            b[i]-=f*b[j];
        }
    }
    C result{};
    for(unsigned j=0;j<3;++j)result+=std::conj(original[j])*b[j];
    return std::max(0.,result.real());
}

struct Capture {
    unsigned boundary=0;
    double previous_rf=0,next_rf=0;
    std::array<C,samples> input{};
};
struct Study {
    std::vector<Capture> captures;
    double noise_power=0;
};

Study make_capture(unsigned count,unsigned seed,double loss,bool signal,
                   double retune_error_seconds) {
    h::Options opts;opts.power_w=40;opts.data_power_w=1;opts.callsign="N0CALL";
    double mean=0;
    for(unsigned i=0;i<20;++i) {
        h::PowerReference ref;ref.epoch=1800000000;ref.begin_seconds=2*i;
        ref.end_seconds=2*(i+1);ref.helper_start_seconds=0;ref.helper_end_seconds=40;
        mean+=h::mean_power(opts,true,&ref).total;
    }
    const double high_fraction=h::pilot_power_fraction/(mean/20);
    const double noise_density_dbm=10*std::log10(1.380649e-23*290*1000)+10;
    const double cn0=10*std::log10(40*1000*high_fraction)-loss-noise_density_dbm;
    Study result;result.noise_power=fs/std::pow(10.,cn0/10);
    const double sigma=std::sqrt(result.noise_power/2);
    auto schedule=helper_measurement_hops::hf50(seed);
    h::Plan plan{2400,.4,0,40,950,550,true};
    h::Generator generator(opts,plan,1800000000,true);
    std::mt19937_64 random(seed+0x82415U);
    std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> angle(-pi,pi);
    std::array<double,100> hop_phase{};
    for(auto& p:hop_phase)p=angle(random);
    // Independent phase per hop. Samples are chronologically ordered; a
    // phase remains fixed within a hop, including across sampled boundaries.
    for(unsigned k=1;k<=count;++k) {
        Capture c;c.boundary=k;c.previous_rf=schedule.frequency(k-1);c.next_rf=schedule.frequency(k);
        const double boundary=static_cast<double>(k)*.4;
        const double true_gate=boundary/true_audio+true_arrival;
        const double retune=true_gate+retune_error_seconds;
        for(unsigned i=0;i<samples;++i) {
            const double r=first+static_cast<double>(i)/fs;
            const double t=boundary+r, tx=true_audio*(t-true_arrival);
            const bool previous=t<retune;
            const double rf=previous?c.previous_rf:c.next_rf;
            const double p=hop_phase[previous?k-1:k]+tau*(rf*.8e-6)*(t-retune);
            const C helper=generator.mix_analytic({},tx,static_cast<unsigned>(fs),40.L,0.L)/
                (.4*h::pilot_amplitude_fraction);
            c.input[i]=(signal?helper*std::polar(1.,p):C{})+
                       C{sigma*normal(random),sigma*normal(random)};
        }
        result.captures.push_back(c);
    }
    return result;
}

struct Result {
    bool detected=false,truth_covered=false;
    double maximum=0,threshold=0,best=0,lower=0,upper=0;
    unsigned supported=0;
};

Result estimate(const Study& study,double model_audio) {
    std::array<double,candidates> score{};
    for(const auto& c:study.captures) {
        const double nominal=static_cast<double>(c.boundary)*.4;
        for(unsigned trial=0;trial<candidates;++trial) {
            const double candidate=step*trial;
            const double boundary=nominal/model_audio+candidate;
            std::array<std::array<std::array<C,3>,3>,2> gram{};
            std::array<std::array<C,3>,2> rhs{};
            for(unsigned i=0;i<samples;++i) {
                const double t=nominal+first+static_cast<double>(i)/fs;
                const bool previous=t<boundary;
                const unsigned side=previous?0:1;
                const double dt=std::abs(t-boundary),rf=previous?c.previous_rf:c.next_rf;
                const double p=gate(dt,.004),a=gate(dt,.008);
                std::array<C,3> x{};
                for(unsigned j=0;j<3;++j) {
                    const double freq=pilots[j]*model_audio+rf*.8e-6;
                    x[j]=(j==2?a:p)*std::polar(1.,std::remainder(tau*freq*(t-nominal),tau));
                    rhs[side][j]+=std::conj(x[j])*c.input[i];
                }
                for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)
                    gram[side][j][k]+=std::conj(x[j])*x[k];
            }
            for(unsigned side=0;side<2;++side)
                score[trial]+=projection(gram[side],rhs[side])/study.noise_power;
        }
    }
    const auto best=static_cast<unsigned>(std::max_element(score.begin(),score.end())-score.begin());
    Result result;result.maximum=score[best];result.best=best*step;
    // Gamma noise-energy upper guard with a conservative trial union. The
    // nuisance model is three complex coefficients on each side of each hop.
    const double shape=6*study.captures.size(),look=std::log(candidates/1e-5);
    result.threshold=shape+2*std::sqrt(shape*look)+2*look;
    result.detected=result.maximum>result.threshold;
    if(result.detected) {
        // Relative profile-loglikelihood support is exploratory and conditional
        // on known noise, RF/audio rate and the assumed stationary ramp shapes.
        constexpr double delta=4.5; // one-parameter 3 sigma scale, not calibrated
        result.lower=std::numeric_limits<double>::infinity();result.upper=-result.lower;
        for(unsigned i=0;i<candidates;++i)if(score[i]>=result.maximum-delta) {
            result.lower=std::min(result.lower,i*step);result.upper=std::max(result.upper,i*step);
            ++result.supported;
        }
        // A retained grid sample represents its complete half-step cell. The
        // expansion prevents the grid spacing from creating a zero-width
        // interval and is not a statistical coverage calibration.
        result.lower=std::max(0.,result.lower-step/2);
        result.upper=std::min((candidates-1)*step,result.upper+step/2);
        result.truth_covered=result.lower<=true_arrival && true_arrival<=result.upper;
    }
    return result;
}

void run(unsigned count,unsigned seed,double loss,bool signal,double retune_us,double model_audio_ppm) {
    const auto capture=make_capture(count,seed,loss,signal,retune_us*1e-6);
    const auto result=estimate(capture,1+model_audio_ppm*1e-6);
    std::cout<<std::setprecision(9)<<"helper_envelope mode=pilot_and_auxiliary_nuisance"
      <<" seed="<<seed<<" dwells="<<count<<" path_loss_db="<<loss<<" helper_w=40"
      <<" high_cn0_db_hz="<<10*std::log10(fs/capture.noise_power)
      <<" signal="<<signal<<" retune_offset_us="<<retune_us
      <<" true_audio_ppm=50 model_audio_ppm="<<model_audio_ppm
      <<" detected="<<result.detected<<" score="<<result.maximum<<" threshold="<<result.threshold;
    if(result.detected)std::cout<<" best_us="<<result.best*1e6
      <<" error_us="<<(result.best-true_arrival)*1e6
      <<" support_lower_us="<<result.lower*1e6<<" support_upper_us="<<result.upper*1e6
      <<" support_width_us="<<(result.upper-result.lower)*1e6
      <<" half_alias_us="<<.5e6/(2400-550)<<" support_points="<<result.supported
      <<" truth_covered="<<result.truth_covered;
    std::cout<<'\n';
    if(!signal && result.detected)throw std::runtime_error("noise-only ramp false admission");
    if(signal && loss==100 && retune_us==0 && model_audio_ppm==50 &&
       (!result.detected || std::abs(result.best-true_arrival)>step/2))
        throw std::runtime_error("strong matched ramp control failed");
}
}

int main(int argc,char** argv) {
    try {
        if(argc<2 || argc>3)throw std::invalid_argument("expected --short or --study [seed]");
        const std::string mode=argv[1];
        // Boundary 90 would require samples after 36 seconds; retain only the
        // 89 interior boundaries fully visible in the frozen lead observation.
        const unsigned count=mode=="--short"?5:mode=="--study"?89:0;
        if(!count)throw std::invalid_argument("expected --short or --study [seed]");
        const unsigned seed=argc==3?static_cast<unsigned>(std::stoul(argv[2])):901;
        if(mode=="--short")run(count,seed,100,true,0,50);
        run(count,seed,180,true,0,50);
        run(count,seed,160,true,0,50);
        run(count,seed,180,false,0,50);
        run(count,seed,180,true,500,50);
        run(count,seed,180,true,0,53);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Helper envelope feasibility: "<<e.what()<<'\n';return 1;}
}
