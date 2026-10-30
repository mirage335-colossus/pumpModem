#include "datapump/helper_measurement.hpp"
#include "helper_measurement_sim.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <future>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

using datapump::helper::HelperMeasurement;
using datapump::helper::HelperMeasurementConfig;
using namespace helper_measurement_sim;
namespace {
void require(bool b,const char* m) {if(!b)throw std::runtime_error(m);}
double delta(double a,double b) {return std::remainder(a-b,tau);}
HelperMeasurementConfig config(double half_width, double diffusion=0) {
    // Bound applies to the full search: +/- 3 oscillator model deviations.
    return {24000,550,2400,2400-half_width,2400+half_width,300,diffusion};
}
double audio_truth(const Link& c,double nominal_time) {
    const double rx=1+c.rx_audio_ppm*1e-6;
    return ((1+(c.tx_audio_ppm+c.audio_drift_ppm_per_s*nominal_time/rx)*1e-6)/rx-1)*1e6;
}
double shift_truth(const Link& c,double nominal_time) {
    const double rx=1+c.rx_audio_ppm*1e-6;
    return (c.tx_shift_ppm-c.rx_shift_ppm+c.shift_drift_ppm_per_s*nominal_time/rx)/rx;
}
void edge_probe() {
    Link c;c.transmit_high_dbm=-94; // About 30 dB-Hz at the 40 dB path.
    c.seed=303;Source s(c);HelperMeasurement receiver(config(9000.72));
    unsigned valid=0,truth_acquired=0;
    for(unsigned i=0;i<2;i++) {
        const auto d=s.generate(i);
        const auto m=receiver.observe(d.samples,d.shift_hz,d.first_seconds);
        if(m.valid) {
            ++valid;
            truth_acquired+=std::abs(m.high_frequency_hz-d.high_mid_hz)<2 &&
                            std::abs(m.low_frequency_hz-d.low_mid_hz)<2;
        }
    }
    std::cout<<"edge seconds=0.8 cn0_db_hz="<<high_cn0_db_hz(c)<<" valid="<<valid
        <<"/2 truth_within_2hz_both_pilots="<<truth_acquired<<"/2 (exploratory; not a 36s sensitivity limit)\n";
}
// Reconstruct nominal-clock complex samples from the measured affine model.
// y(t)=x(t/a)*exp(-i*2pi*b*t/a), with source interpolation at t/a.
std::vector<std::complex<float>> correct(const Dwell& d,double a,double b,double fs) {
    // Windowed-sinc interpolation limits clock-conversion bias at the highest
    // captured frequency. The source index begins after the filter guard.
    constexpr int radius=16;
    const auto sinc=[](double x) {return std::abs(x)<1e-12?1.:std::sin(std::numbers::pi*x)/(std::numbers::pi*x);};
    std::vector<std::complex<float>> out;
    const auto nmax=static_cast<std::size_t>((d.samples.size()-2*radius-1)*a);
    out.reserve(nmax);
    for(std::size_t n=0;n<nmax;n++) {
        const double index=radius+n/a;const auto center=static_cast<long>(std::floor(index));
        std::complex<double> value{};double weight=0;
        for(int k=-radius+1;k<=radius;k++) {
            const double x=index-(center+k);
            const double w=sinc(x)*sinc(x/radius);
            value+=std::complex<double>(d.samples[static_cast<std::size_t>(center+k)])*w;
            weight+=w;
        }
        value/=weight;
        const double phase=-tau*b*static_cast<double>(n)/(a*fs);
        const auto z=value*std::polar(1.,phase);
        out.emplace_back(static_cast<float>(z.real()),static_cast<float>(z.imag()));
    }
    return out;
}

void ideal_controls() {
    for(int mode=0;mode<3;mode++) {
        Link c;c.noise=false;c.auxiliary=false;c.hopping=mode==2;c.phase_noise_degrees_per_sqrt_s=0;
        c.tx_audio_ppm=mode==0?0:40;c.rx_audio_ppm=mode==0?0:-13;
        c.tx_shift_ppm=mode==1?0:50;c.rx_shift_ppm=0;
        c.audio_drift_ppm_per_s=0;c.shift_drift_ppm_per_s=0;
        Source source(c);HelperMeasurement receiver(config(9000.72));unsigned valid=0;
        for(unsigned i=0;i<8;i++) {
            const auto d=source.generate(i);const auto m=receiver.observe(d.samples,d.shift_hz,d.first_seconds);
            require(m.valid,"ideal per-dwell acquisition failed");++valid;
            require(std::abs(m.high_frequency_hz-d.high_mid_hz)<.02,"ideal high frequency error");
            require(std::abs(m.low_frequency_hz-d.low_mid_hz)<.02,"ideal low frequency error");
            require(std::abs(delta(m.high_phase_radians,d.high_phase_radians))<.02,"ideal high phase error");
            require(std::abs(delta(m.low_phase_radians,d.low_phase_radians))<.02,"ideal low phase error");
        }
        const auto s=receiver.snapshot();require(s.valid && valid==8,"ideal clock snapshot unavailable");
        require(std::abs(s.audio_ppm-audio_truth(c,s.time_seconds))<.05,"ideal audio estimate error");
        require(std::abs(s.shift_ppm-shift_truth(c,s.time_seconds))<.00001,"ideal shift estimate error");
        const auto held=source.generate(8);
        const auto a=1+audio_truth(c,held.first_seconds+.18)*1e-6;
        const auto b=shift_truth(c,held.first_seconds+.18)*held.shift_hz*1e-6;
        auto fixed=correct(held,a,b,c.sample_rate);
        HelperMeasurement post({c.sample_rate,550,2400,2390,2410,300});
        const auto corrected=post.observe(fixed,1,held.first_seconds);
        require(corrected.valid && std::abs(corrected.high_frequency_hz-2400)<.001,
                "ideal resampling introduced excessive frequency error");
    }
    std::cout<<"ideal controls: shift-only, audio-only, combined hopping PASS\n";
}
struct Case {const char* name;double shift_hz,shift_ppm,shift_bound_ppm,phase_noise;bool hopping=true;};
struct ScenarioResult {std::string output; bool covered,heldout_covered;};
ScenarioResult scenario(Case cs,unsigned seed) {
    Link c;c.shift_center_hz=cs.shift_hz;c.tx_shift_ppm=cs.shift_ppm;c.phase_noise_degrees_per_sqrt_s=cs.phase_noise;
    c.tx_audio_ppm=37;c.rx_audio_ppm=-13;c.seed=seed;c.transmit_high_dbm=-54; // ~70 dB-Hz
    c.hop_step_hz=cs.shift_hz>1e9?1000000:600000;
    c.hopping=cs.hopping;
    // Dated observations include a small deterministic trend, not just offsets.
    c.audio_drift_ppm_per_s=.015;c.shift_drift_ppm_per_s=cs.shift_ppm*.00005;
    const double width=3*((cs.shift_hz+2*c.hop_step_hz)*cs.shift_bound_ppm+2400*100)*1e-6;
    Source source(c);HelperMeasurement receiver(config(width,cs.phase_noise*std::numbers::pi/180/std::sqrt(1+c.rx_audio_ppm*1e-6)));
    const auto started=std::chrono::steady_clock::now();
    double sq_phase=0,max_phase=0,sq_high=0,sq_hop_phase=0,sq_hop_frequency=0;
    double measured_cn0_sum=0,measured_low_cn0_sum=0;
    unsigned valid=0, hop_count=0;
    std::optional<Dwell> previous_truth;
    std::optional<datapump::helper::HelperDwellMeasurement> previous_measurement;
    unsigned previous_index=0;
    for(unsigned i=0;i<90;i++) {
        const auto d=source.generate(i);const auto m=receiver.observe(d.samples,d.shift_hz,d.first_seconds);
        if(!m.valid)continue;
        const double e=delta(m.high_phase_radians,d.high_phase_radians);
        sq_phase+=e*e;max_phase=std::max(max_phase,std::abs(e));
        sq_high+=std::pow(m.high_frequency_hz-d.high_mid_hz,2);
        measured_cn0_sum+=m.high_cn0_db_hz;measured_low_cn0_sum+=m.low_cn0_db_hz;++valid;
        if(previous_measurement && previous_truth && i==previous_index+1) {
            const double boundary=i*.4;
            const auto jump=datapump::helper::helper_hop_phase_change(*previous_measurement,m,boundary);
            require(jump.has_value(),"adjacent hop phase unavailable");
            const double expected=std::remainder(d.high_phase_radians-previous_truth->high_phase_radians-
                tau*(previous_truth->high_mid_hz*(boundary-previous_truth->first_seconds)+
                d.high_mid_hz*(d.first_seconds-boundary)),tau);
            const double phase_error=delta(*jump,expected);
            sq_hop_phase+=phase_error*phase_error;
            const double hop_error=(m.high_frequency_hz-previous_measurement->high_frequency_hz)-
                (d.high_mid_hz-previous_truth->high_mid_hz);
            sq_hop_frequency+=hop_error*hop_error;
            require(std::abs(phase_error)<.2,"random hop phase change error");
            require(std::abs(hop_error)<.2,"deterministic hop frequency change error");
            ++hop_count;
        }
        previous_truth=d;previous_measurement=m;previous_index=i;
    }
    const auto s=receiver.snapshot();require(s.valid && valid>=80,"36 second scenario did not acquire enough dwells");
    const double ae=s.audio_ppm-audio_truth(c,s.time_seconds);
    const double se=s.shift_ppm-shift_truth(c,s.time_seconds);
    require(std::abs(ae)<3,"36 second relative audio estimate error");
    require(std::abs(se*1e-6*cs.shift_hz)<.03,"36 second relative shift estimate error");
    const auto held=source.generate(90);
    const double dt=(held.first_seconds+.18)-s.time_seconds;
    const double audio=s.audio_ppm+s.audio_drift_ppm_per_second*dt;
    const double shift=s.shift_ppm+s.shift_drift_ppm_per_second*dt;
    const double a=1+audio*1e-6,b=held.shift_hz*shift*1e-6;
    auto transformed=correct(held,a,b,c.sample_rate);
    HelperMeasurement post({c.sample_rate,550,2400,2390,2410,300,
        cs.phase_noise*std::numbers::pi/180/std::sqrt(1+c.rx_audio_ppm*1e-6)});
    const auto m=post.observe(transformed,1,held.first_seconds);
    require(m.valid,"independent held-out corrected signal not acquired");
    const double residual=m.high_frequency_hz-c.high_hz;
    require(std::abs(residual)<.03,"held-out correction leaves excessive residual");
    const double data_f=1500;
    const auto prediction=receiver.predict(data_f,cs.shift_hz,s.time_seconds);
    const auto future=receiver.predict(c.high_hz,held.shift_hz,held.first_seconds+.18,.36);
    require(prediction.valid && future.valid,"dated frequency prediction unavailable");
    const double predicted_sigma_hz=prediction.mean_sigma_hz;
    const double data_error=(data_f*ae+cs.shift_hz*se)*1e-6;
    const bool covered=std::abs(data_error)<=3*predicted_sigma_hz;
    const double heldout_halfwidth=3*std::sqrt((future.mean_sigma_hz*future.mean_sigma_hz+
       future.aperture_process_sigma_hz*future.aperture_process_sigma_hz)/(a*a)+m.high_sigma_hz*m.high_sigma_hz);
    const bool heldout_covered=std::abs(residual)<=heldout_halfwidth;
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::ostringstream out;out<<std::setprecision(8)<<cs.name<<" seed="<<seed<<" seconds=36 observed="<<valid
        <<" hopping="<<c.hopping<<" FS="<<c.sample_rate<<" capture_hz=[-12000,12000] search_half_hz="<<width
        <<" path_loss_db="<<c.path_loss_db<<" high_tx_dbm="<<c.transmit_high_dbm
        <<" high_thermal_cn0_db_hz="<<high_cn0_db_hz(c)<<" low_thermal_cn0_db_hz="<<high_cn0_db_hz(c)-3
        <<" high_effective_cn0_db_hz="<<measured_cn0_sum/valid
        <<" low_effective_cn0_db_hz="<<measured_low_cn0_sum/valid
        <<" audio_ppm="<<s.audio_ppm<<" audio_error_ppm="<<ae<<" audio_sigma_ppm="<<s.audio_sigma_ppm
        <<" shift_ppm="<<s.shift_ppm<<" shift_error_ppm="<<se<<" shift_sigma_ppm="<<s.shift_sigma_ppm
        <<" data1500_shift_hz="<<s.shift_ppm*1e-6*cs.shift_hz+s.audio_ppm*1e-6*data_f
        <<" data1500_conditional_3sigma_hz="<<3*predicted_sigma_hz
        <<" data1500_model_error_hz="<<data_error<<" conditional_3sigma_covers="<<covered
        <<" mix_correction_hz="<<-b/a<<" output_input_samples_ratio="<<a
        <<" heldout_high_residual_hz="<<residual<<" heldout_3sigma_hz="<<heldout_halfwidth
        <<" heldout_3sigma_covers="<<heldout_covered
        <<" high_dwell_frequency_rmse_hz="<<std::sqrt(sq_high/valid)
        <<" pilot_phase_rms_deg="<<std::sqrt(sq_phase/valid)*180/std::numbers::pi
        <<" hop_phase_change_rms_error_deg="<<std::sqrt(sq_hop_phase/hop_count)*180/std::numbers::pi
        <<" hop_frequency_change_rmse_hz="<<std::sqrt(sq_hop_frequency/hop_count)
        <<" pilot_phase_max_deg="<<max_phase*180/std::numbers::pi<<" wall_s="<<elapsed<<'\n';
    return {out.str(),covered,heldout_covered};
}
}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"--suite";
        if(mode!="--suite"&&mode!="--feasibility-only")throw std::runtime_error("expected --suite or --feasibility-only");
        edge_probe();if(mode=="--feasibility-only")return 0;
        ideal_controls();
        const Case cases[]={{"HF-free",30e6,80,100,.5},{"HF-IC7100",30e6,.8,1,.05},{"5.8GHz-GPSDO",5.8e9,.00008,.0001,.005}};
        std::vector<std::future<std::vector<ScenarioResult>>> jobs;
        for(unsigned i=0;i<3;i++)jobs.push_back(std::async(std::launch::async,[&,i] {
            std::vector<ScenarioResult> results;
            for(unsigned seed=0;seed<3;seed++) results.push_back(scenario(cases[i],731+i+seed*11));
            if(i==0) results.push_back(scenario({"HF-free-no-FHSS",30e6,80,100,.5,false},799));
            return results;
        }));
        unsigned covers=0,heldout_covers=0,total=0;
        for(auto& job:jobs)for(const auto& r:job.get()) {
            std::cout<<r.output;covers+=r.covered;heldout_covers+=r.heldout_covered;++total;
        }
        std::cout<<"Conditional 3-sigma modeled DATA-frequency coverage: "<<covers<<"/"<<total
            <<", heldout-pilot coverage: "<<heldout_covers<<"/"<<total
            <<"; three seeds per FHSS scenario and one non-FHSS control, three threads. Small sample only; covariance excludes unmodeled process evolution and is not a coverage guarantee.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"helper measurement failure: "<<e.what()<<'\n';return 1;}
}
