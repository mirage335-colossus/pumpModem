#include "helper_live_probe_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace hp = helper_live_probe;
using C = std::complex<double>;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main() try {
    constexpr std::array<double,3> offsets{-.002,0.,.002};
    std::array<std::unique_ptr<hp::Source>,3> noisy, clean;
    for (std::size_t k=0;k<offsets.size();++k) {
        hp::Config config;
        config.seed=1720100001; config.expanded_schedule_key=true;
        config.noise_seed=1730100001;
        config.rf_retune_offset_seconds=offsets[k];
        // Make the jump itself unambiguous; independent clock, thermal noise,
        // random hop phases and the actual Helper/probe waveforms remain.
        config.relative_shift_ppm=0; config.phase_noise_degrees_per_sqrt_second=0;
        noisy[k]=std::make_unique<hp::Source>(config);
        config.noise=false;
        clean[k]=std::make_unique<hp::Source>(config);
    }
    std::array<hp::Dwell,3> signal,without_noise;
    double max_waveform_error=0,max_noise_error=0,max_probe_error=0;
    std::array<bool,3> saw_phase_jump{};
    for (unsigned dwell=0;dwell<=95;++dwell) {
        for (std::size_t k=0;k<offsets.size();++k) {
            signal[k]=noisy[k]->generate(dwell);
            without_noise[k]=clean[k]->generate(dwell);
        }
        // Test all samples around the first random RF hop and in a full
        // payload-bearing probe dwell; intermediate dwells still advance the
        // real fixture generators and their keyed RNG histories.
        if (dwell!=1 && dwell!=95) continue;
        for (unsigned i=0;i<hp::dwell_samples;++i) {
            const C base_common(without_noise[1].common_phasor[i]);
            const C base_wave=C(without_noise[1].samples[i])/base_common;
            const C base_probe=C(without_noise[1].clean_probe[i])/base_common;
            const C base_noise=C(signal[1].samples[i])-C(without_noise[1].samples[i]);
            const double nominal=dwell*hp::dwell_seconds+static_cast<double>(i)/hp::sample_rate;
            for (std::size_t k=0;k<offsets.size();++k) {
                const C common(without_noise[k].common_phasor[i]);
                require(signal[k].common_phasor[i]==without_noise[k].common_phasor[i],
                        "thermal setting changed RF oscillator truth");
                max_waveform_error=std::max(max_waveform_error,std::abs(C(without_noise[k].samples[i])/common-base_wave));
                max_probe_error=std::max(max_probe_error,std::abs(C(without_noise[k].clean_probe[i])/common-base_probe));
                max_noise_error=std::max(max_noise_error,std::abs(C(signal[k].samples[i])-C(without_noise[k].samples[i])-base_noise));
                if (dwell==1 && i && clean[k]->rf_hop_at_input(nominal)!=
                                      clean[k]->rf_hop_at_input(nominal-1./hp::sample_rate)) {
                    const double t=(nominal+.31/hp::sample_rate)/clean[k]->rx_scale();
                    const double boundary=.00321+offsets[k]+.4/(1+37e-6);
                    require(t>=boundary && t-boundary<1./hp::sample_rate/clean[k]->rx_scale()+1e-10,
                            "RF phase hop occurs at wrong physical date");
                    const C prev(without_noise[k].common_phasor[i-1]);
                    require(std::abs(std::arg(common*std::conj(prev)))>.01,"missing random phase jump");
                    saw_phase_jump[k]=true;
                }
            }
        }
    }
    require(std::all_of(saw_phase_jump.begin(),saw_phase_jump.end(),[](bool x){return x;}),
            "did not exercise each RF retune");
    require(max_waveform_error<2e-6 && max_probe_error<2e-6,
            "changing RF retune shifted Helper or payload waveform calendar");
    require(max_noise_error<3e-6,"independent thermal-noise samples changed with RF retune");
    for (double value : {-.0101,.0101}) {
        hp::Config bad;bad.rf_retune_offset_seconds=value;
        bool rejected=false;try { hp::Source source(bad); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"out-of-range RF retune offset accepted");
    }
    // An early first retune can already have happened before sample zero; it
    // must initialize directly on hop zero without inventing a previous hop.
    hp::Config initial;initial.arrival_offset_seconds=0;initial.rf_retune_offset_seconds=-.002;
    initial.relative_shift_ppm=0;initial.phase_noise_degrees_per_sqrt_second=0;
    hp::Source early(initial);
    require(early.rf_hop_at_input(0)==0,"incorrect initial early-retune hop");
    const auto first=early.generate(0);
    require(std::abs(C(first.common_phasor[0])-std::polar(1.,initial.initial_rf_phase_radians))<1e-6,
            "early initial retune invented a phase jump");
    std::cout<<"helper_retune_offset passed=1 offsets_seconds=-.002,0,.002"
             <<" nominal_rf_edge_verified=1 payload_dwell=95"
             <<" max_calendar_waveform_error="<<max_waveform_error
             <<" max_probe_error="<<max_probe_error<<" max_noise_error="<<max_noise_error<<'\n';
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
