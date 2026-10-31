#include "datapump/helper_boundary_tracker.hpp"
#include "datapump/helper_measurement.hpp"
#include "helper_live_probe_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace hp=helper_live_probe;
namespace h=datapump::helper;
using Tracker=h::HelperBoundaryTracker;
using C=std::complex<double>;
using S=std::complex<float>;
constexpr double fs=hp::sample_rate,tau=hp::tau;
constexpr std::size_t guard=480,aperture=8640,edge_half=432;
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
double cpu(){return static_cast<double>(std::clock())/CLOCKS_PER_SEC;}
double smooth(double t){t=std::clamp(t,0.,1.);return t*t*t*(10+t*(-15+6*t));}

Tracker::Hop hop_model(const hp::Dwell& d,const h::HelperDwellMeasurement& m){
    Tracker::Hop out;
    out.observation_first_sample=d.first_seconds*fs+guard;
    out.observation_end_sample=out.observation_first_sample+aperture;
    if(!m.valid)return out;
    const auto data=std::span<const S>(d.samples.data()+guard,aperture);
    const double duration=static_cast<double>(aperture)/fs;
    out.pilots[0]=Tracker::settled_pilot(data,out.observation_first_sample,fs,m.high_frequency_hz,
        std::sqrt(2/(std::pow(10.,m.high_cn0_db_hz/10)*duration)),m.high_sigma_hz);
    out.pilots[1]=Tracker::settled_pilot(data,out.observation_first_sample,fs,m.low_frequency_hz,
        std::sqrt(2/(std::pow(10.,m.low_cn0_db_hz/10)*duration)),m.low_sigma_hz);
    return out;
}

void deterministic(){
    Tracker tracker({fs,.008,.00025,.004,16});
    std::vector<S> x(2*edge_half);constexpr double center=9600,offset=72;
    Tracker::Hop before,after;
    before.observation_first_sample=480;before.observation_end_sample=9120;
    after.observation_first_sample=10080;after.observation_end_sample=18720;
    before.pilots={Tracker::Pilot{true,4800,2421,std::polar(1.,.42),0,0},Tracker::Pilot{true,4800,571,std::polar(.71,1.2),0,0}};
    after.pilots={Tracker::Pilot{true,14400,2427,std::polar(1.,-2.1),0,0},Tracker::Pilot{true,14400,577,std::polar(.71,-.34),0,0}};
    for(std::size_t i=0;i<x.size();++i){const double n=center-edge_half+static_cast<double>(i),d=n-center-offset;
        C z{};for(const auto& p:(d<0?before:after).pilots)z+=p.amplitude_at_anchor*std::polar(1.,tau*p.frequency_hz*(n-p.anchor_sample)/fs);
        x[i]=S(z*smooth(std::abs(d)/(fs*.004)));}
    Tracker::Input in{x,center-edge_half,center,1,before,after,19200};
    const auto r=tracker.observe(in);require(r.available,"deterministic boundary unavailable");
    require(std::abs(r.boundary_sample-center-offset)<.001,"fresh-phase ramp boundary estimate wrong");
    require(r.prior_first_sample==center-192&&r.prior_end_sample==center+192,"prior ambiguity silently narrowed");
    require(!tracker.observe(in).available,"reused boundary silently double counted");
    auto changed=in;changed.predicted_boundary_sample+=1;
    require(!tracker.observe(changed).available,"overlapping boundary edge silently double counted");
    changed=in;changed.predicted_boundary_sample+=9600;require(!tracker.observe(changed).available,"uncovered search window admitted");
    Tracker clean({fs,.008,.00025,.004,16});
    for(unsigned test=0;test<6;++test){changed=in;
        if(test==0)changed.before.observation_first_sample=std::numeric_limits<double>::quiet_NaN();
        if(test==1)changed.after.observation_end_sample=changed.after.observation_first_sample;
        if(test==2)changed.before.pilots[0].anchor_sample=changed.before.observation_first_sample-1;
        if(test==3)changed.latest_observation_end_sample=changed.after.observation_end_sample-1;
        if(test==4)changed.before.pilots[0].amplitude_at_anchor={1e308,0};
        if(test==5)changed.after.observation_end_sample=std::numeric_limits<double>::infinity();
        require(!clean.observe(changed).available,"malformed boundary prediction admitted");
    }
    const auto after_invalid=clean.observe(in);
    require(after_invalid.available&&after_invalid.boundaries==1&&after_invalid.boundary_sample==r.boundary_sample,
            "invalid input poisoned retained boundary state");
    bool nonintegral=false;try{Tracker bad({fs,.0081,.00025,.004,16});}catch(const std::invalid_argument&){nonintegral=true;}
    require(nonintegral,"nonintegral grid enlarged configured prior");
    std::cout<<"boundary_tracker_deterministic passed=true workspace_bytes="<<tracker.workspace_bytes()<<"\n";
}

void study(double loss,double arrival,unsigned seeds,unsigned count,bool noise_only){
    double sum=0,base=0,worst=0,bias=0,gen_cpu=0,receiver_cpu=0,tracking_cpu=0;
    unsigned n=0,available=0,signal_observations=0;std::size_t work=0;
    for(unsigned seed=0;seed<seeds;++seed){
        hp::Config cfg;cfg.path_loss_db=loss;cfg.arrival_offset_seconds=arrival;
        cfg.seed=1810000001ULL+seed;cfg.noise_seed=cfg.seed+20'000'000ULL;cfg.expanded_schedule_key=true;
        hp::Source source(cfg);Tracker tracker({fs,.008,.00025,.004,16});
        h::HelperMeasurementConfig mc{fs,550,2400,2280,2520,300,.05*std::numbers::pi/180};
        h::HelperMeasurement receiver(mc);
        h::Options options;options.power_w=cfg.helper_w;options.data_power_w=1;options.callsign=cfg.callsign;
        h::Generator generator(options,h::Plan{2400,.4,0,cfg.helper_w,950,550,true},cfg.epoch,true);
        hp::Dwell previous;Tracker::Hop previous_model;
        for(unsigned dwell=0;dwell<count;++dwell){
            const auto generated=cpu();auto d=source.generate(dwell);
            if(noise_only)for(std::size_t j=0;j<d.samples.size();++j){const double t=d.first_seconds+static_cast<double>(j)/fs;
                const C helper=generator.mix_analytic({},source.tx_time(t),static_cast<unsigned>(fs),40.L,0.L)/(.4*h::pilot_amplitude_fraction);
                d.samples[j]=S(C(d.samples[j])-helper*C(d.common_phasor[j])-C(d.clean_probe[j]));}
            gen_cpu+=cpu()-generated;const auto observed=cpu();
            const auto m=receiver.observe(std::span<const S>(d.samples.data()+guard,aperture),d.shift_hz,d.first_seconds+guard/fs);
            const auto model=hop_model(d,m);const auto snapshot=receiver.snapshot();receiver_cpu+=cpu()-observed;
            signal_observations+=m.valid;
            if(dwell&&m.valid&&previous_model.pilots[0].valid){
                std::vector<S> edge(2*edge_half);
                std::copy(previous.samples.end()-static_cast<std::ptrdiff_t>(edge_half),previous.samples.end(),edge.begin());
                std::copy_n(d.samples.begin(),edge_half,edge.begin()+edge_half);
                // Receiver-estimated relative audio rate removes secular
                // schedule drift. The unknown constant arrival remains the
                // entire +/-8 ms search, without a true-source-time input.
                const double rate=snapshot.valid?1+snapshot.audio_ppm*1e-6:
                    (m.high_frequency_hz-m.low_frequency_hz)/(2400-550);
                const double nominal=static_cast<double>(dwell)*hp::dwell_samples;
                Tracker::Input in{edge,nominal-edge_half,nominal/rate,rate,previous_model,model,
                                  static_cast<double>(dwell+1)*hp::dwell_samples};
                const auto began=cpu();const auto result=tracker.observe(in);tracking_cpu+=cpu()-began;work+=result.sample_products;
                if(result.available){++available;
                    if(dwell>=std::min(8U,count-1)){
                        // Truth is used only after receiver selection.
                        const double truth=(cfg.arrival_offset_seconds+static_cast<double>(dwell)*.4/(1+cfg.tx_audio_ppm*1e-6))*(1+cfg.rx_audio_ppm*1e-6)*fs-cfg.fractional_input_sample;
                        const double error=(result.boundary_sample-truth)/fs;
                        sum+=error*error;bias+=error;worst=std::max(worst,std::abs(error));
                        const double original=(nominal-truth)/fs;base+=original*original;++n;
                    }
                }
            }
            previous=std::move(d);previous_model=model;
        }
    }
    std::cout<<std::setprecision(10)<<"boundary_tracker_study loss_db="<<loss<<" arrival_ms="<<arrival*1000
      <<" seeds="<<seeds<<" duration_per_seed_seconds="<<count*.4<<" noise_only="<<noise_only
      <<" valid_pilot_observations="<<signal_observations<<" boundary_estimates="<<available<<" scored="<<n
      <<" boundary_rms_ms="<<(n?1000*std::sqrt(sum/n):0)<<" nominal_rms_ms="<<(n?1000*std::sqrt(base/n):0)
      <<" bias_ms="<<(n?1000*bias/n:0)<<" worst_ms="<<worst*1000
      <<" prior_half_width_ms=8 profile_covariance_calibrated=false retained_boundaries=16 grid_ms=.25"
      <<" receiver_cpu_seconds="<<receiver_cpu<<" boundary_cpu_seconds="<<tracking_cpu
      <<" generation_cpu_seconds="<<gen_cpu<<" sample_products="<<work<<"\n";
}
int main(int argc,char**argv){try{
    deterministic();if(argc==1)return 0;
    require(std::string_view(argv[1])=="--study","unknown option");
    const unsigned seeds=argc>2?static_cast<unsigned>(std::stoul(argv[2])):8;
    const unsigned count=argc>3?static_cast<unsigned>(std::stoul(argv[3])):16;
    require(seeds>=1&&seeds<=8&&count>=2&&count<=32,"bounded study count");
    for(const double arrival:{.00321,.00021,.0078})study(180,arrival,seeds,count,false);
    study(160,.00321,seeds,count,false);study(180,.00321,seeds,count,true);
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
