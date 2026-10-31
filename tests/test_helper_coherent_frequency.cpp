#include "datapump/helper_carrier.hpp"
#include "datapump/helper_coherent_frequency.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Exact production waveform in an independent flat physical-time channel.
// This fixture has NO FHSS and retains phase across apertures. Its two-path
// perturbation is illustrative, not a measured lunar scattering model.
namespace {
namespace h=datapump::helper;
constexpr double fs=24000,rf=5.8e9,tau=2*std::numbers::pi;
constexpr double tx_audio_ppm=.00004,rx_audio_ppm=-.00003;
constexpr double rx_scale=1+rx_audio_ppm*1e-6;
constexpr double shift_ppm=.00008;
struct Case {
    double watts=1500,loss=220,rate=0,acceleration=0,spread=0,multipath=.0;
    double q_degrees=.005;bool signal=true,phase_reset=false;unsigned seed=1001;
    double audio_center_ppm=0;bool tight_center_search=false;
    double declared_q_degrees=.005;
};
double tx_clock(const Case& c){return 1+(tx_audio_ppm+c.audio_center_ppm)*1e-6;}
struct Dwell {double start=0;std::vector<std::complex<float>> samples;};
h::Options options(double watts){h::Options o;o.power_w=watts;o.data_power_w=1;o.callsign="N0CALL";return o;}
double fraction() {
    static const double f=[] {
        const auto o=options(1500);double total=0;
        for(unsigned i=0;i<18;++i){h::PowerReference r;r.epoch=1800000000;r.begin_seconds=2*i;r.end_seconds=2*(i+1);total+=h::mean_power(o,false,&r).total;}
        return h::pilot_power_fraction/(total/18);
    }();return f;
}
double cn0(const Case& c) {
    const double density=10*std::log10(1.380649e-23*290*1000)+10;
    return 10*std::log10(c.watts*1000*fraction())-c.loss-density;
}
double cpu() {const auto t=std::clock();if(t==static_cast<std::clock_t>(-1))throw std::runtime_error("CPU clock unavailable");return static_cast<double>(t)/CLOCKS_PER_SEC;}
class Source {
public:
    explicit Source(Case c):c_(c),rng_(c.seed),generator_(options(c.watts),h::Plan{2400,.4,0,c.watts,950,550,false},1800000000,true) {
        sigma_=std::sqrt(fs*rx_scale/(2*std::pow(10.,cn0(c)/10)));
    }
    Dwell next(unsigned n) {
        if(n!=next_++)throw std::logic_error("nonsequential physical source");
        Dwell d;d.start=n*.4+.02;d.samples.resize(8640);
        std::normal_distribution<double> normal;std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
        const double q=c_.q_degrees*std::numbers::pi/180;
        brown_+=q*std::sqrt(.02/rx_scale)*normal(rng_);
        if(c_.phase_reset&&n)phase_+=uniform(rng_);
        for(std::size_t k=0;k<d.samples.size();++k) {
            const double t=(d.start+static_cast<double>(k)/fs)/rx_scale;
            const double centered=t-18;
            const double cycles=rf*shift_ppm*1e-6*t+.5*c_.rate*t*t+c_.acceleration*centered*centered*centered/6;
            auto signal=c_.signal?generator_.mix_analytic({},t*tx_clock(c_),static_cast<std::uint32_t>(fs))/(.4*h::pilot_amplitude_fraction):std::complex<double>{};
            signal*=std::polar(1.,phase_+tau*cycles+brown_);
            if(c_.multipath!=0)signal*=(1.+c_.multipath*std::polar(1.,tau*c_.spread*t+.83))/std::sqrt(1+c_.multipath*c_.multipath);
            d.samples[k]={static_cast<float>(signal.real()+sigma_*normal(rng_)),static_cast<float>(signal.imag()+sigma_*normal(rng_))};
            brown_+=q/std::sqrt(fs*rx_scale)*normal(rng_);
        }
        brown_+=q*std::sqrt(.02/rx_scale)*normal(rng_);return d;
    }
    double truth(double nominal,double nominal_time) const {
        const double t=nominal_time/rx_scale;
        return (nominal*tx_clock(c_)+rf*shift_ppm*1e-6+c_.rate*t+.5*c_.acceleration*(t-18)*(t-18))/rx_scale;
    }
private:
    Case c_;std::mt19937_64 rng_;h::Generator generator_;double sigma_=0,brown_=0,phase_=.31;unsigned next_=0;
};
struct Row {bool detected=false,qualified=false;double error=0,width=0,cpu=0,held=0,score=0;};
Row run(const Case& c,unsigned count=90) {
    Source source(c);std::vector<Dwell> input;
    for(unsigned i=0;i<count+(count==90?5:0);++i)input.push_back(source.next(i));
    h::HelperCoherentFrequencyConfig cfg;
    const double prior=3*(rf*.0001+2400*.0001)*1e-6;
    cfg.measurement={fs,550,2400,2400-prior,2400+prior,.0001,c.declared_q_degrees*std::numbers::pi/180/std::sqrt(rx_scale)};
    cfg.audio_center_ppm=c.audio_center_ppm;
    if(c.tight_center_search){cfg.measurement.high_search_min_hz=2400*(1+c.audio_center_ppm*1e-6)+.42;cfg.measurement.high_search_max_hz=2400*(1+c.audio_center_ppm*1e-6)+.50;}
    cfg.rate_search_half_hz_per_second=.01;
    double begin=cpu();h::HelperCoherentFrequency receiver(cfg);Row out;out.cpu+=cpu()-begin;
    for(unsigned i=0;i<count;++i){begin=cpu();const bool stored=receiver.observe(input[i].samples,rf,input[i].start);(void)receiver.snapshot((i+1)*.4);out.cpu+=cpu()-begin;
        if(!stored)throw std::runtime_error("coherent no-hop geometry rejected");}
    begin=cpu();const auto s=receiver.snapshot(count*.4);const double time=(count-1)*.4+.2;
    const auto p=receiver.predict(1500,time,count*.4);out.cpu+=cpu()-begin;
    out.detected=s.detected;out.qualified=p.valid;out.score=s.consistency_score;
    if(count<90&&(s.detected||p.valid))throw std::runtime_error("coherent receiver qualified before36s");
    if(p.valid){out.error=p.frequency_hz-source.truth(1500,time);out.width=p.half_width_hz;
        if(count==90){begin=cpu();const auto held=receiver.predict(2400,37.8,36);out.cpu+=cpu()-begin;if(held.valid)out.held=held.frequency_hz-source.truth(2400,37.8);}}
    begin=cpu();const bool changed=receiver.observe(input.front().samples,rf+1,40);out.cpu+=cpu()-begin;
    if(changed)throw std::runtime_error("coherent receiver accepted an RF retune");
    return out;
}
void report(const std::string& name,Case c,unsigned seeds,bool assert_clean=false) {
    unsigned detected=0,qualified=0,covered=0,narrow=0,false_narrow=0;double error=0,width=0,held=0,cost=0;
    for(unsigned i=0;i<seeds;++i){c.seed=1001+i*37;const auto r=run(c);detected+=r.detected;qualified+=r.qualified;
        if(r.qualified){covered+=std::abs(r.error)<=r.width;narrow+=r.width<=.1;false_narrow+=r.width<=.1&&std::abs(r.error)>.1;error=std::max(error,std::abs(r.error));width=std::max(width,r.width);held=std::max(held,std::abs(r.held));}
        cost=std::max(cost,r.cpu);
        if(r.qualified&&(std::abs(r.error)>r.width||(r.width<=.1&&std::abs(r.error)>.1)))
            throw std::runtime_error("coherent no-hop fit claimed an incorrect precision interval");
        if(assert_clean&&(!r.detected||!r.qualified||std::abs(r.error)>.1||std::abs(r.error)>r.width||r.width>.1))throw std::runtime_error("clean coherent no-hop control failed");
        if(!c.signal&&r.detected)throw std::runtime_error("coherent noise-only false detection");
        if(c.phase_reset&&r.qualified)throw std::runtime_error("random phase resets qualified as coherent");
    }
    if(c.signal&&!c.phase_reset&&c.acceleration==0&&qualified==0)
        throw std::runtime_error("coherent positive control has no qualified estimate");
    std::cout<<std::setprecision(9)<<"coherent_nohop case="<<name<<" watts="<<c.watts<<" loss_db="<<c.loss<<" high_cn0_db_hz="<<cn0(c)<<" pilot_fraction="<<fraction()
        <<" seeds="<<seeds<<" detected="<<detected<<" qualified="<<qualified<<" narrow_0p1="<<narrow<<" covered="<<covered<<" false_narrow="<<false_narrow
        <<" max_error_hz="<<(qualified?std::to_string(error):"unavailable")<<" max_halfwidth_hz="<<(qualified?std::to_string(width):"unavailable")
        <<" heldout_model_error_hz="<<(qualified?std::to_string(held):"unavailable")<<" max_cpu_s="<<cost<<'\n';
}
void budget_control() {
    h::HelperCoherentFrequencyConfig cfg;
    cfg.measurement={fs,550,2400,2392,2408,.0001,0};
    h::HelperCoherentFrequency receiver(cfg);const std::vector<std::complex<float>> zero(8640);
    for(unsigned i=0;i<90;++i)if(!receiver.observe(zero,rf,i*.4+.02))throw std::runtime_error("budget geometry failed");
    const auto s=receiver.snapshot(36);
    if(!s.work_limited||s.detected||s.uncertainty_valid)throw std::runtime_error("coherent search budget was not explicit");
}
}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"--suite";
        if(mode=="--probe") {
            for(bool signal:{false,true}){Case c;c.signal=signal;const auto r=run(c,2);std::cout<<"coherent_short signal="<<signal<<" detected="<<r.detected<<" qualified="<<r.qualified<<" cpu_s="<<r.cpu<<'\n';}
            return 0;
        }
        if(mode=="--phase") {
            Case phase;phase.q_degrees=.05;report("phase_x10_undeclared",phase,8);
            phase.q_degrees=.5;report("phase_x100_undeclared",phase,8);
            phase.declared_q_degrees=.5;report("phase_x100_declared",phase,8);
            return 0;
        }
        if(mode!="--suite"&&mode!="--study")throw std::invalid_argument("expected --probe, --suite, --study or --phase");
        budget_control();
        const unsigned seeds=mode=="--suite"?2:8;
        report("15k-clean",Case{15000},seeds,true);report("1p5k-clean",Case{1500},seeds,true);
        Case center;center.audio_center_ppm=50;center.tight_center_search=true;report("known_audio_center",center,seeds,true);
        Case noise;noise.signal=false;report("noise",noise,seeds);
        Case jump;jump.phase_reset=true;report("random_phase_reset",jump,seeds);
        if(mode=="--study"){
            Case drift;drift.rate=.0005;report("rate_0p0005",drift,seeds,true);drift.rate=.005;report("rate_0p005",drift,seeds,true);
            Case curvature;curvature.acceleration=.0014;report("curvature",curvature,seeds);
            for(double spread:{.1,.25,.5}){Case path;path.multipath=.5;path.spread=spread;report("two_path_"+std::to_string(spread),path,seeds);}
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"coherent Helper: "<<e.what()<<'\n';return 1;}
}
