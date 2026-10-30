#pragma once

// Independent sample-level physical-time source for the Helper measurement
// tests. This fixture does not call the receiver or use its search results.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

namespace helper_measurement_sim {
constexpr double tau = 2 * std::numbers::pi;
struct Link {
    double sample_rate = 24000;
    double low_hz = 550;
    double high_hz = 2400;
    double transmit_high_dbm = -74;
    double path_loss_db = 40;
    double temperature_kelvin = 290;
    double noise_figure_db = 10;
    double tx_audio_ppm = 40;
    double rx_audio_ppm = -13;
    double tx_shift_ppm = 50;
    double rx_shift_ppm = 0;
    double audio_drift_ppm_per_s = 0;
    double shift_drift_ppm_per_s = 0;
    double phase_noise_degrees_per_sqrt_s = .5;
    bool noise = true;
    bool auxiliary = true;
    bool hopping = true;
    std::uint64_t seed = 173;
    double shift_center_hz = 30e6;
    double hop_step_hz = 600000;
};
struct Dwell {
    std::vector<std::complex<float>> samples;
    double first_seconds = 0;
    double shift_hz = 0;
    // Truth for independent checks only. Phase is at the first aperture sample.
    double high_phase_radians = 0;
    double low_phase_radians = 0;
    double high_mid_hz = 0;
    double low_mid_hz = 0;
};
inline double noise_density_dbm_hz(const Link& c) {
    constexpr double boltzmann=1.380649e-23;
    return 10*std::log10(boltzmann*c.temperature_kelvin*1000) + c.noise_figure_db;
}
inline double high_cn0_db_hz(const Link& c) {
    return c.transmit_high_dbm-c.path_loss_db-noise_density_dbm_hz(c);
}
inline double hop_frequency(const Link& c, unsigned dwell) {
    // A known commanded RF calendar, deliberately independent of impairment.
    constexpr int hop[]{-2, 1, -1, 2, 0};
    return c.shift_center_hz + (c.hopping ? hop[dwell%5]*c.hop_step_hz : 0);
}
class Source {
public:
    explicit Source(Link config): c_(config), rng_(config.seed) {}
    Dwell generate(unsigned dwell) {
        Dwell d;
        // Observe the settled central 360 ms of each 400 ms dwell.
        d.first_seconds = dwell*.4+.02;
        d.shift_hz = hop_frequency(c_,dwell);
        const auto n = static_cast<std::size_t>(std::llround(c_.sample_rate*.36));
        d.samples.resize(n);
        if(dwell!=next_dwell_++) throw std::invalid_argument("simulated dwells must be generated in order");
        std::uniform_real_distribution<double> uniform(-std::numbers::pi,std::numbers::pi);
        std::normal_distribution<double> normal;
        const double rx_scale=1+c_.rx_audio_ppm*1e-6;
        if(c_.hopping && dwell>0) hop_phase_+=uniform(rng_);
        // The command calendar is in receiver nominal seconds; ADC physical
        // time differs by rx_scale. Audio phase remains continuous at hops.
        const double start_physical=d.first_seconds/rx_scale;
        const double phase_noise=c_.phase_noise_degrees_per_sqrt_s*std::numbers::pi/180;
        brownian_phase_+=phase_noise*std::sqrt(std::max(0.,start_physical-brownian_time_))*normal(rng_);
        const auto instantaneous=[&](double nominal,double t) {
            // Evaluate in physical seconds, then convert the resulting cycles
            // per physical second to cycles per nominal ADC second.
            return (nominal*(1+1e-6*(c_.tx_audio_ppm+c_.audio_drift_ppm_per_s*t)) +
                   d.shift_hz*1e-6*(c_.tx_shift_ppm-c_.rx_shift_ppm+c_.shift_drift_ppm_per_s*t))/rx_scale;
        };
        const double center=(d.first_seconds+(n-1)/(2*c_.sample_rate))/rx_scale;
        d.high_mid_hz=instantaneous(c_.high_hz,center);
        d.low_mid_hz=instantaneous(c_.low_hz,center);
        const auto rf_integral=[&](double hz,double t0,double t1) {
            return hz*1e-6*((c_.tx_shift_ppm-c_.rx_shift_ppm)*(t1-t0)+
                .5*c_.shift_drift_ppm_per_s*(t1*t1-t0*t0));
        };
        double rf_cycles=0;
        for(unsigned prior=0;prior<dwell;++prior) {
            rf_cycles+=rf_integral(hop_frequency(c_,prior),prior*.4/rx_scale,(prior+1)*.4/rx_scale);
        }
        rf_cycles+=rf_integral(d.shift_hz,dwell*.4/rx_scale,start_physical);
        const auto phase_at_start=[&](double nominal,double fixed_phase) {
            const double audio_cycles=nominal*(start_physical*(1+c_.tx_audio_ppm*1e-6)+
                .5e-6*c_.audio_drift_ppm_per_s*start_physical*start_physical);
            return fixed_phase+hop_phase_+brownian_phase_+tau*(audio_cycles+rf_cycles);
        };
        double phase_high=phase_at_start(c_.high_hz,.71);
        double phase_low=phase_at_start(c_.low_hz,-.42);
        double phase_aux=phase_at_start(950.,.20);
        d.high_phase_radians=std::remainder(phase_high,tau);
        d.low_phase_radians=std::remainder(phase_low,tau);
        const double cn0=std::pow(10., high_cn0_db_hz(c_)/10);
        // The physical ADC rate is Fs*rx_scale. Complex noise has
        // E[|n|^2]=Fs_physical*N0 relative to unit high-pilot power;
        // each Gaussian component has half that variance.
        const double sigma=c_.noise?std::sqrt(c_.sample_rate*rx_scale/(2*cn0)):0;
        const double diffusion=phase_noise/std::sqrt(c_.sample_rate*rx_scale);
        constexpr double low_amp=0.7079457843841379; // -3 dB power
        for(std::size_t i=0;i<n;++i) {
            auto signal=std::polar(1.,phase_high)+std::polar(low_amp,phase_low);
            if(c_.auxiliary) {
                // Independent representative auxiliary modulation: 8ms dwell
                // boundary gates, smooth reversal 152–168ms, and nine 20ms
                // APSK-like transitions after 212ms. It is deliberately not
                // decoded and is not an authenticated metadata packet.
                const double local=.02+i/c_.sample_rate;
                const auto smooth=[](double x) {const auto t=std::clamp(x,0.,1.);return t*t*t*(10+t*(-15+6*t));};
                const auto smooth7=[](double x) {const auto t=std::clamp(x,0.,1.);return t*t*t*t*(35+t*(-84+t*(70-20*t)));};
                const double gate=smooth(std::min(local,.4-local)/.008);
                double reversal=1;
                if(local>=.152 && local<.16) reversal=1-smooth7((local-.152)/.008);
                else if(local>=.16) reversal=-smooth7((local-.16)/.008);
                std::complex<double> symbol{1,0};
                if(local>=.212) {
                    const auto position=(local-.212)/.020;
                    const auto index=std::min(8,static_cast<int>(position));
                    const auto point=[&](int k) {
                        if(k<0)return std::complex<double>{1,0};
                        const double angle=std::numbers::pi/4+(k*3+static_cast<int>(dwell))*std::numbers::pi/2;
                        return std::polar(k%2?.75:1.,angle);
                    };
                    symbol=point(index-1)+(point(index)-point(index-1))*smooth7(position-index);
                }
                constexpr double aux_amp=0.3548133892335755; // -9 dB power
                signal+=aux_amp*gate*reversal*symbol*std::polar(1.,phase_aux);
            }
            d.samples[i]={static_cast<float>(signal.real()+sigma*normal(rng_)),
                          static_cast<float>(signal.imag()+sigma*normal(rng_))};
            const double t=(d.first_seconds+i/c_.sample_rate)/rx_scale;
            const double random_phase=diffusion*normal(rng_);
            brownian_phase_+=random_phase;
            // Integrate the linear frequency exactly across each sample.
            phase_high+=tau*instantaneous(c_.high_hz,t+.5/(c_.sample_rate*rx_scale))/c_.sample_rate+random_phase;
            phase_low +=tau*instantaneous(c_.low_hz,t+.5/(c_.sample_rate*rx_scale))/c_.sample_rate+random_phase;
            phase_aux +=tau*instantaneous(950.,t+.5/(c_.sample_rate*rx_scale))/c_.sample_rate+random_phase;
        }
        brownian_time_=(d.first_seconds+n/c_.sample_rate)/rx_scale;
        return d;
    }
private:
    Link c_;
    std::mt19937_64 rng_;
    unsigned next_dwell_=0;
    double hop_phase_=.13;
    double brownian_phase_=0;
    double brownian_time_=0;
};
} // namespace helper_measurement_sim
