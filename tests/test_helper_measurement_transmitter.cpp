#include "datapump/helper_carrier.hpp"
#include "datapump/helper_measurement.hpp"
#include "helper_measurement_hops.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <future>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// This fixture renders the production three-tone waveform, independent of the
// receiver. The ADC and TX clocks and RF phasor are evaluated in physical
// time, and neither the acquired frequency nor its truth is used as a search
// center. The RF hop calendar is receiver-known; random hop phases are not.
namespace {
namespace h = datapump::helper;
constexpr double fs=24000, tau=2*std::numbers::pi;
constexpr double tx_ppm=37, rx_ppm=-13;
constexpr double tx_scale=1+tx_ppm*1e-6, rx_scale=1+rx_ppm*1e-6;
struct Case {
    const char* label;
    double helper_w, path_loss_db, center_hz, shift_ppm, prior_ppm, diffusion_deg;
    bool hopping;
    std::uint64_t seed;
};
struct Capture {
    std::vector<std::complex<float>> samples;
    double first_seconds, shift_hz, high_truth, low_truth;
};
double noise_density_dbm_hz() {
    return 10*std::log10(1.380649e-23*290*1000)+10;
}
class Source {
public:
    explicit Source(Case c):c_(c),rng_(c.seed),
        hops_(c_.hopping ? (c_.center_hz>1e9
            ? helper_measurement_hops::eu20(static_cast<unsigned>(c_.seed))
            : helper_measurement_hops::hf50(static_cast<unsigned>(c_.seed)))
            : helper_measurement_hops::Profile{}),
        options_(),plan_(),generator_(make_generator()) {
        // mean_power describes the active composite waveform; the high pilot
        // is scaled by its straight-tone fraction rather than all Helper W.
        const double fraction=h::pilot_power_fraction/h::mean_power(options_,c_.hopping).total;
        high_tx_dbm_=10*std::log10(c_.helper_w*1000*fraction);
        cn0_=std::pow(10.,(high_tx_dbm_-c_.path_loss_db-noise_density_dbm_hz())/10);
        high_amplitude_=plan_.helper_gain*h::pilot_amplitude_fraction;
    }
    double cn0_db_hz() const {return 10*std::log10(cn0_);}
    double high_tx_dbm() const {return high_tx_dbm_;}
    Capture capture(unsigned dwell) {
        if (dwell!=next_dwell_) throw std::logic_error("capture must follow dwell calendar");
        ++next_dwell_;
        const double shift=c_.hopping?hops_.frequency(dwell):c_.center_hz;
        std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
        if(c_.hopping && dwell) phase_rf_+=uniform(rng_);
        const double local_start=.02/rx_scale, duration=.4/rx_scale;
        const double shift_hz=shift*c_.shift_ppm*1e-6;
        // Common phase diffusion runs over discarded gaps too.
        const double q=c_.diffusion_deg*std::numbers::pi/180;
        std::normal_distribution<double> normal;
        phase_brownian_+=q*std::sqrt(local_start)*normal(rng_);
        const double sigma=high_amplitude_*std::sqrt((fs*rx_scale)/(2*cn0_));
        const double diffusion_step=q/std::sqrt(fs*rx_scale);
        Capture out;
        out.first_seconds=dwell*.4+.02;out.shift_hz=shift;
        out.high_truth=(2400*tx_scale+shift_hz)/rx_scale;
        out.low_truth=(550*tx_scale+shift_hz)/rx_scale;
        const auto n=static_cast<std::size_t>(fs*.36);
        out.samples.resize(n);
        for(std::size_t i=0;i<n;i++) {
            const double nominal=out.first_seconds+i/fs;
            const double phys=nominal/rx_scale;
            const double elapsed=local_start+i/(fs*rx_scale);
            const auto wave=generator_.mix_analytic({},phys*tx_scale,static_cast<std::uint32_t>(fs));
            const auto lo=std::polar(1.,phase_rf_+tau*shift_hz*elapsed+phase_brownian_);
            const auto z=wave*lo+std::complex<double>(sigma*normal(rng_),sigma*normal(rng_));
            out.samples[i]={static_cast<float>(z.real()),static_cast<float>(z.imag())};
            phase_brownian_+=diffusion_step*normal(rng_);
        }
        // Remaining 20 ms of the dwell and following aperture's leading gap
        // are advanced independently; RX nominal clock sets dwell boundaries.
        const double remaining=duration-local_start-n/(fs*rx_scale);
        phase_brownian_+=q*std::sqrt(std::max(0.,remaining))*normal(rng_);
        phase_rf_+=tau*shift_hz*duration;
        return out;
    }
private:
    h::Generator make_generator() {
        options_.power_w=c_.helper_w; options_.data_power_w=1; options_.callsign="N0CALL";
        plan_={2400,.4,0,c_.helper_w,950,550,c_.hopping};
        return h::Generator(options_,plan_,1800000000,true);
    }
    Case c_;std::mt19937_64 rng_;helper_measurement_hops::Profile hops_;h::Options options_;h::Plan plan_;h::Generator generator_;
    double high_tx_dbm_=0,cn0_=0,high_amplitude_=0,phase_rf_=.37,phase_brownian_=0;
    unsigned next_dwell_=0;
};
h::HelperMeasurementConfig config(const Case& c) {
    const double maximum_shift=c.hopping?(c.center_hz>1e9?5868.4e6:31.225e6):c.center_hz;
    const double width=3*(maximum_shift*c.prior_ppm+2400*100)*1e-6;
    return {fs,550,2400,2400-width,2400+width,300,
            c.diffusion_deg*std::numbers::pi/180/std::sqrt(rx_scale)};
}
std::string run(Case c,unsigned count,bool strong) {
    Source source(c);h::HelperMeasurement receiver(config(c));
    unsigned valid=0,correct=0;
    double high_error=0,low_error=0;
    for(unsigned i=0;i<count;i++) {
        const auto d=source.capture(i);
        const auto m=receiver.observe(d.samples,d.shift_hz,d.first_seconds);
        if(m.valid) {
            ++valid;
            const double eh=m.high_frequency_hz-d.high_truth,el=m.low_frequency_hz-d.low_truth;
            if(std::abs(eh)<2 && std::abs(el)<2) ++correct;
            high_error+=eh*eh;low_error+=el*el;
        }
    }
    std::ostringstream out;
    out<<std::setprecision(9)<<c.label<<" fhss="<<c.hopping<<" dwells="<<count
             <<" helper_w="<<c.helper_w<<" path_loss_db="<<c.path_loss_db
             <<" high_tx_dbm="<<source.high_tx_dbm()<<" high_cn0_db_hz="<<source.cn0_db_hz()
             <<" valid="<<valid<<" correct_within_2hz="<<correct;
    if(valid)out<<" high_rmse_hz="<<std::sqrt(high_error/valid)<<" low_rmse_hz="<<std::sqrt(low_error/valid);
    const auto state=receiver.snapshot();
    if(state.valid) {
        const double audio_truth=(tx_scale/rx_scale-1)*1e6;
        const double shift_truth=c.shift_ppm/rx_scale;
        out<<" audio_ppm="<<state.audio_ppm<<" audio_error_ppm="<<(state.audio_ppm-audio_truth)
           <<" shift_ppm="<<state.shift_ppm<<" shift_error_ppm="<<(state.shift_ppm-shift_truth)
           <<" data_1500_error_hz="<<((state.audio_ppm-audio_truth)*1500+(state.shift_ppm-shift_truth)*c.center_hz)*1e-6;
        if(strong && (std::abs(state.audio_ppm-audio_truth)>100 ||
            std::abs(((state.audio_ppm-audio_truth)*1500+(state.shift_ppm-shift_truth)*c.center_hz)*1e-6)>.15))
            throw std::runtime_error("actual Helper transmitter fitted clock error");
    } else out<<" fitted_model=unavailable";
    out<<'\n';
    if(strong && (valid!=count || correct!=count || !state.valid))throw std::runtime_error("actual Helper transmitter qualified example failed");
    return out.str();
}
double heldout_high_frequency(const Capture& d,double predicted) {
    // A separate held-out high-pilot fit: the search center is the receiver's
    // prediction, not simulator truth. A narrow ±2 Hz range explicitly limits
    // this diagnostic and its measurement error is not a confidence bound.
    const auto power=[&](double hz) {
        std::complex<double> sum{},rot=1,step=std::polar(1.,-tau*hz/fs);
        for(const auto x:d.samples) {sum+=std::complex<double>(x)*rot;rot*=step;}
        return std::norm(sum);
    };
    double center=predicted,best=-1;
    for(int i=-8;i<=8;i++) {
        const double f=predicted+i*.25,p=power(f);
        if(p>best){best=p;center=f;}
    }
    double lo=std::max(predicted-2,center-.25),hi=std::min(predicted+2,center+.25);
    constexpr double g=.6180339887498948482;
    double a=hi-g*(hi-lo),b=lo+g*(hi-lo),pa=power(a),pb=power(b);
    for(int i=0;i<25;i++) {
        if(pa>pb){hi=b;b=a;pb=pa;a=hi-g*(hi-lo);pa=power(a);}
        else {lo=a;a=b;pa=pb;b=lo+g*(hi-lo);pb=power(b);}
    }
    return (lo+hi)/2;
}
std::string pooled(Case c,unsigned count) {
    Source source(c);
    h::HelperPooledConfig pc;pc.measurement=config(c);
    h::HelperPooledMeasurement receiver(pc);
    unsigned stored=0;
    for(unsigned i=0;i<count;i++) {
        const auto d=source.capture(i);
        stored+=receiver.observe(d.samples,d.shift_hz,d.first_seconds);
    }
    const auto result=receiver.snapshot(count*.4);
    if(stored!=count)throw std::runtime_error("pooled actual waveform dropped an aperture");
    if(count==2 && result.valid)throw std::runtime_error("pooled actual waveform admitted before 36 seconds");
    if(count==90 && !result.valid)throw std::runtime_error("pooled actual waveform 36 second evidence missing");
    std::ostringstream out;
    out<<std::setprecision(9)<<"actual-microwave-pooled fhss="<<c.hopping<<" seed="<<c.seed
       <<" seconds="<<count*.4<<" helper_w="<<c.helper_w<<" path_loss_db="<<c.path_loss_db
       <<" high_cn0_db_hz="<<source.cn0_db_hz()<<" stored="<<stored<<" qualified="<<result.valid
       <<" boundary_limited="<<result.boundary_limited<<" uncertainty_valid="<<result.uncertainty_valid
       <<" high_evidence="<<result.high_evidence<<" low_evidence="<<result.low_evidence
       <<" evidence_threshold="<<result.evidence_threshold;
    if(result.valid) {
        const auto& state=result.clock;
        if(result.boundary_limited && (result.uncertainty_valid || state.valid ||
           std::isfinite(state.audio_sigma_ppm) || std::isfinite(state.shift_sigma_ppm)))
            throw std::runtime_error("boundary-limited result exposed invalid uncertainty");
        if(!result.boundary_limited && (!result.uncertainty_valid || !state.valid))
            throw std::runtime_error("interior pooled result missing uncertainty");
        const double audio_truth=(tx_scale/rx_scale-1)*1e6,shift_truth=c.shift_ppm/rx_scale;
        const double data_error=((state.audio_ppm-audio_truth)*1500+(state.shift_ppm-shift_truth)*c.center_hz)*1e-6;
        if(std::abs(state.audio_ppm-audio_truth)>250 || std::abs(data_error)>.4)
            throw std::runtime_error("actual pooled clock estimate outside example tolerance");
        out<<" audio_ppm="<<state.audio_ppm<<" audio_error_ppm="<<state.audio_ppm-audio_truth
           <<" shift_ppm="<<state.shift_ppm<<" data_1500_error_hz="<<data_error
           <<" audio_sigma_ppm="<<state.audio_sigma_ppm<<" shift_sigma_ppm="<<state.shift_sigma_ppm;
        const auto held=source.capture(count);
        const double dt=held.first_seconds+.18-state.time_seconds;
        const double audio=state.audio_ppm+dt*state.audio_drift_ppm_per_second;
        const double shift=state.shift_ppm+dt*state.shift_drift_ppm_per_second;
        const double scale=1+audio*1e-6,offset=held.shift_hz*shift*1e-6;
        const double expected=scale*2400+offset;
        const double fitted=heldout_high_frequency(held,expected);
        if(std::abs(expected-held.high_truth)>.5 || std::abs(fitted-expected)>.8)
            throw std::runtime_error("actual pooled held-out high frequency outside example tolerance");
        out<<" heldout_shift_hz="<<held.shift_hz<<" mix_correction_hz="<<-offset/scale
           <<" output_input_ratio="<<scale<<" heldout_model_error_hz="<<expected-held.high_truth
           <<" heldout_observed_residual_hz="<<fitted-expected;
    }
    out<<'\n';return out.str();
}
}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"--suite";
        if(mode!="--suite"&&mode!="--probe"&&mode!="--weak-36"&&mode!="--pooled-probe"&&mode!="--pooled-36")throw std::runtime_error("expected --suite, --probe, --weak-36, --pooled-probe or --pooled-36");
        if(mode=="--pooled-probe"||mode=="--pooled-36") {
            const unsigned count=mode=="--pooled-probe"?2:90;
            std::vector<std::future<std::string>> jobs;
            for(bool hop:{false,true})jobs.push_back(std::async(std::launch::async,[hop,count] {
                std::string rows;
                const unsigned trials=count==2?1:3;
                for(unsigned i=0;i<trials;i++)
                    rows+=pooled({"microwave",15000,220,5.8e9,.00008,.0001,.005,hop,721+i*11},count);
                return rows;
            }));
            for(auto& job:jobs)std::cout<<job.get();
            return 0;
        }
        if(mode=="--weak-36") {
            std::vector<std::future<std::string>> jobs;
            for(bool hop:{false,true})jobs.push_back(std::async(std::launch::async,[hop] {
                return run({"HF-36s",40,180,30e6,.8,1,.05,hop,723},90,true);
            }));
            for(auto& job:jobs)std::cout<<job.get();
            return 0;
        }
        for(bool hop:{false,true}) {
            std::cout<<run({"microwave",15000,220,5.8e9,.00008,.0001,.005,hop,721},2,false);
            std::cout<<run({"HF",40,180,30e6,.8,1,.05,hop,723},2,false);
        }
        if(mode=="--suite") {
            for(bool hop:{false,true})
                std::cout<<run({"strong-actual-transmitter",40,140,30e6,.8,1,.05,hop,741},8,true);
            std::vector<std::future<std::string>> jobs;
            for(bool hop:{false,true})jobs.push_back(std::async(std::launch::async,[hop] {
                return run({"HF-36s",40,180,30e6,.8,1,.05,hop,723},90,true);
            }));
            for(auto& job:jobs)std::cout<<job.get();
        }
        return 0;
    } catch(const std::exception& ex) {std::cerr<<"actual Helper transmitter test: "<<ex.what()<<'\n';return 1;}
}
