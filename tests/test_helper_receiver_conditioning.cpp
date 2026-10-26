#include "datapump/helper_carrier.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/streaming_modem.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi,helper_hz=2262.5007,helper_amplitude=.072889;
constexpr double relative_data_power=.00217;
struct Summary {
    std::size_t updates=0,phase=0,timing=0,tighter=0;
    double raw_frequency_ratio=0,joint_frequency_ratio=0,raw_phase_ratio=0,joint_phase_ratio=0;
    double max_frequency_change=0,max_phase_change=0,min_frequency_ci=1e9,max_phase_forecast=0;
    bool chronology=true,same_authority=true,covered=true;
};
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
double wrapped(double phase) {return std::remainder(phase,tau);}
void measure(const helper::Reference& r,double truth_phase,double truth_frequency,bool joint,Summary& out) {
    const auto frequency_ratio=std::abs(r.frequency_hz-truth_frequency)/r.frequency_uncertainty_hz;
    if(joint)out.joint_frequency_ratio=std::max(out.joint_frequency_ratio,frequency_ratio);
    else out.raw_frequency_ratio=std::max(out.raw_frequency_ratio,frequency_ratio);
    if(r.phase_valid) {
        const auto ratio=std::abs(wrapped(r.phase_radians-truth_phase))/r.phase_uncertainty_radians;
        if(joint)out.joint_phase_ratio=std::max(out.joint_phase_ratio,ratio);
        else out.raw_phase_ratio=std::max(out.raw_phase_ratio,ratio);
    }
}
Summary run(const std::vector<float>& pcm,std::uint32_t rate,bool has_helper,
            double lower=1184.4825,double upper=1815.5175,bool expected_fallback=false,
            double truth_frequency=helper_hz) {
    helper::Receiver raw(2262.5,90.45);
    helper::Receiver joint(2262.5,90.45,{},helper::OccupiedBand{lower,upper});
    Summary out;
    for(std::size_t first=0;first<pcm.size();first+=137) {
        const auto block=std::span<const float>(pcm).subspan(first,std::min<std::size_t>(137,pcm.size()-first));
        raw.push(block,rate,first);joint.push(block,rate,first);
        const auto a=raw.take_update(),b=joint.take_update();
        if(a.has_value()!=b.has_value())out.same_authority=false;
        if(!a||!b)continue;
        ++out.updates;out.phase+=b->phase_valid;out.timing+=b->timing_valid;
        out.chronology&=b->sample<first+block.size()&&(!b->frequency_sample||b->frequency_sample<=b->sample);
        out.same_authority&=a->sample==b->sample&&a->phase_valid==b->phase_valid&&
            a->timing_valid==b->timing_valid&&a->frequency_valid==b->frequency_valid&&
            a->dwell_start_seconds==b->dwell_start_seconds&&a->dwell_uncertainty_seconds==b->dwell_uncertainty_seconds&&
            a->metadata.has_value()==b->metadata.has_value();
        const auto df=std::abs(a->frequency_hz-b->frequency_hz);
        const auto dp=std::abs(wrapped(a->phase_radians-b->phase_radians));
        out.max_frequency_change=std::max(out.max_frequency_change,df);
        out.max_phase_change=std::max(out.max_phase_change,dp);
        out.tighter+=b->frequency_uncertainty_hz<a->frequency_uncertainty_hz;
        out.min_frequency_ci=std::min(out.min_frequency_ci,b->frequency_uncertainty_hz);
        out.max_phase_forecast=std::max(out.max_phase_forecast,b->phase_uncertainty_radians+tau*.2*b->frequency_uncertainty_hz);
        if(expected_fallback)out.same_authority&=df==0&&dp==0&&
            a->phase_uncertainty_radians==b->phase_uncertainty_radians&&
            a->frequency_uncertainty_hz==b->frequency_uncertainty_hz;
        if(has_helper) {
            const auto phase=wrapped(.81+tau*truth_frequency*static_cast<double>(a->sample)/rate);
            measure(*a,phase,truth_frequency,false,out);measure(*b,phase,truth_frequency,true,out);
        }
    }
    out.covered=out.raw_frequency_ratio<=1&&out.joint_frequency_ratio<=1&&
        out.raw_phase_ratio<=1&&out.joint_phase_ratio<=1;
    if(!has_helper)out.same_authority&=out.updates==0&&out.phase==0&&out.timing==0;
    return out;
}
std::vector<float> signal(std::uint32_t rate,double seconds,double interferer,double phase,
                          unsigned seed,bool helper=true,double noise=.0001,double frequency=helper_hz) {
    std::vector<float> pcm(static_cast<std::size_t>(rate*seconds));
    std::mt19937 random(seed);std::normal_distribution<double> gaussian(0,noise);
    for(std::size_t n=0;n<pcm.size();++n) {
        const auto time=static_cast<double>(n)/rate;
        auto x=helper?helper_amplitude*std::cos(tau*frequency*time+.81):0.;
        if(interferer>0)x+=helper_amplitude*std::sqrt(relative_data_power)*std::cos(tau*interferer*time+phase);
        x+=gaussian(random);pcm[n]=static_cast<float>(std::round(x*32767)/32767);
    }
    return pcm;
}
}
int main() {
    try {
        const auto test=[&](const std::string& name,std::vector<float> pcm,std::uint32_t rate,bool helper=true,
                            double lower=1184.4825,double upper=1815.5175,bool fallback=false,
                            bool positive=false,double frequency=helper_hz) {
            const auto s=run(pcm,rate,helper,lower,upper,fallback,frequency);
            if(!s.chronology||!s.same_authority||!s.covered)
                throw std::runtime_error(name+": reference chronology, raw authority or true uncertainty coverage failed");
            if(positive&&(!s.phase||!s.tighter))
                throw std::runtime_error(name+": expected a qualified phase reference and actual refinement");
        };
        test("AWGN",signal(48000,3.2,0,0,17),48000,true,1184.4825,1815.5175,false,true);
        for(const auto frequency:{1275.3,1512.5,1724.7,1184.49,1815.51})for(const auto phase:{0.,.7,1.9}) {
            const auto name="offgrid_"+std::to_string(frequency)+"_"+std::to_string(phase);
            test(name,signal(48000,3.2,frequency,phase,17+static_cast<unsigned>(phase*10)),48000,
                 true,1184.4825,1815.5175,false,frequency==1512.5);
        }
        test("integer_bin",signal(48000,3.2,1500,.7,91),48000);
        test("rate_48001",signal(48001,3.2,1512.5,.7,91),48001);
        test("no_helper_noise",signal(48000,6,0,0,39,false),48000,false);
        test("no_helper_public_band_RFI",signal(48000,6,1512.5,.7,39,false),48000,false);
        test("fallback_overlap",signal(48000,3.2,1512.5,.7,39),48000,true,2200,2325,true);
        test("fallback_outward_overlap",signal(48000,3.2,1512.5,.7,39),48000,true,2100,2171.99,true);
        test("fallback_column_cap",signal(48000,3.2,1512.5,.7,39),48000,true,100,1800,true);
        for(unsigned seed=100;seed<124;++seed) {
            const auto frequency=seed%2?1512.5:0.;
            test("seed_"+std::to_string(seed),signal(48000,3.2,frequency,.13*seed,seed),48000,
                 true,1184.4825,1815.5175,false,frequency>0);
        }
        test("helper_CFO_30Hz",signal(48000,3.2,0,0,91,true,.0001,helper_hz+30),48000,
             true,1184.4825,1815.5175,false,true,helper_hz+30);
        modem::Config config;config.sample_rate=48000;config.carrier_hz=1500;
        config.bandwidth_hz=36;config.spreading_factor=256;config.dsss_factor=10;
        config.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;config.scramble=true;
        config.spreading_seed[0]=9;config.dsss_seed[0]=17;
        modem::StreamingTransmitter transmitter(modem::RawBits{Bytes{0,1,1}},config);
        std::vector<float> data(24*48000);check(transmitter.read(data)==data.size(),"short ordinary waveform");
        double energy=0;for(const auto x:data)energy+=static_cast<double>(x)*x;
        const auto scale=helper_amplitude*std::sqrt(.5*relative_data_power/(energy/data.size()));
        auto combined=signal(48000,24,0,0,723);
        for(std::size_t n=0;n<data.size();++n)combined[n]=static_cast<float>(std::round((combined[n]+scale*data[n])*32767)/32767);
        test("generated_Robust_waveform",std::move(combined),48000,true,1184.4825,1815.5175,false,true);
        std::puts("helper receiver conditioning passed");return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"ERROR,%s\n",error.what());return 2;}
}
