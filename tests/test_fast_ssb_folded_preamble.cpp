#include "fast/ssb_folded_preamble.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

using datapump::fast::detail::SsbFoldedPeak;
using datapump::fast::detail::SsbFoldedPreamble;

namespace {
using Complex=SsbFoldedPreamble::Complex;
constexpr auto tau=2*std::numbers::pi;

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

std::vector<Complex> word(std::size_t size,unsigned seed) {
    std::vector<Complex> result(size);
    std::mt19937 random(seed);
    for(auto& item:result)item={random()&1?1.:-1.,random()&1?1.:-1.};
    return result;
}

void add_path(std::span<Complex> output,std::span<const Complex> reference,
              std::size_t delay,int bin,Complex gain=1.) {
    for(std::size_t i=0;i<output.size();++i) {
        const auto phase=std::polar(1.,tau*bin*static_cast<double>(i)/static_cast<double>(output.size()));
        output[i]+=gain*phase*reference[(i+reference.size()-delay)%reference.size()];
    }
}

SsbFoldedPeak direct(std::span<const Complex> input,std::span<const Complex> reference,
                     double sample_rate,double max_hz) {
    const auto bin_hz=sample_rate/static_cast<double>(input.size());
    const auto edge=static_cast<int>(std::floor(max_hz/bin_hz));
    double observed_energy=0.,template_energy=0.;
    for(const auto value:input)observed_energy+=std::norm(value);
    for(const auto value:reference)template_energy+=std::norm(value);
    template_energy*=static_cast<double>(input.size()/reference.size());
    SsbFoldedPeak best{};
    for(int bin=-edge;bin<=edge;++bin) {
        for(std::size_t delay=0;delay<reference.size();++delay) {
            Complex correlation{};
            for(std::size_t i=0;i<input.size();++i) {
                const auto phase=std::polar(1.,-tau*bin*static_cast<double>(i)/static_cast<double>(input.size()));
                correlation+=input[i]*phase*std::conj(reference[(i+reference.size()-delay)%reference.size()]);
            }
            const auto coherence=std::norm(correlation)/(observed_energy*template_energy);
            if(coherence>best.coherence)best={delay,bin*bin_hz,coherence};
        }
    }
    return best;
}

void check_direct_oracle() {
    const auto reference=word(31,917);
    std::vector<Complex> samples(8*reference.size());
    add_path(samples,reference,30,-11,{.53,.77});
    add_path(samples,reference,8,5,{.27,-.11});
    SsbFoldedPreamble projection(reference,1000.,80.);
    const auto expected=direct(samples,reference,1000.,80.);
    const auto actual=projection.search(samples);
    check(actual && actual->delay_samples==expected.delay_samples &&
          actual->frequency_hz==expected.frequency_hz &&
          std::abs(actual->coherence-expected.coherence)<1e-11,
          "Folded public-word projection differs from direct correlation");
}

void check_shared_geometry() {
    const auto reference=word(352,412);
    SsbFoldedPreamble projection(reference,12000.,280.);
    const auto workspace=projection.workspace_bytes();

    std::vector<Complex> samples(24*reference.size());
    add_path(samples,reference,351,-177,{-2.,.63});
    const auto full=projection.search(samples);
    check(full && full->delay_samples==351 &&
          full->frequency_hz==-177*(12000./static_cast<double>(samples.size())) &&
          std::abs(full->coherence-1)<1e-11,
          "24-word projection lost wrapped phase or negative frequency");

    samples.assign(8*reference.size(),{});
    // Equal paths have opposite one-word Doppler phases. Their periodic lag
    // correlation almost cancels, while the frequency-resolved peaks remain.
    add_path(samples,reference,0,3);
    add_path(samples,reference,137,7);
    Complex lag{};
    double power=0.;
    for(std::size_t i=reference.size();i<samples.size();++i) {
        lag+=samples[i]*std::conj(samples[i-reference.size()]);
        power+=std::norm(samples[i]);
    }
    check(std::abs(lag)/power<.12,"Multipath fixture did not cancel its periodic lag");
    const auto gate=projection.search(samples);
    const auto bin_hz=12000./static_cast<double>(samples.size());
    check(gate && ((gate->delay_samples==0&&gate->frequency_hz==3*bin_hz) ||
                   (gate->delay_samples==137&&gate->frequency_hz==7*bin_hz)) &&
          std::abs(gate->coherence-.5)<1e-11,
          "Eight-word projection missed independently shifted delayed paths");

    std::mt19937 random(4319);
    std::normal_distribution<double> noise;
    for(auto& item:samples)item={noise(random),noise(random)};
    const auto noise_peak=projection.search(samples);
    check(noise_peak && noise_peak->coherence<.04,"Noise resembles the public reference in the fixture");
    for(std::size_t i=0;i<samples.size();++i)
        samples[i]=std::polar(1.,tau*23*static_cast<double>(i)/static_cast<double>(samples.size()));
    const auto tone_peak=projection.search(samples);
    check(tone_peak && tone_peak->coherence<.08,"Tone resembles the public reference in the fixture");
    std::fill(samples.begin(),samples.end(),Complex{});
    add_path(samples,word(reference.size(),1987),73,-9);
    const auto wrong=projection.search(samples);
    check(wrong && wrong->coherence<.08,"Different repeated word resembles the public reference in the fixture");

    std::stop_source stop;
    stop.request_stop();
    check(!projection.search(samples,stop.get_token()),"Cancelled public projection returned a peak");
    std::fill(samples.begin(),samples.end(),Complex{});
    check(!projection.search(samples),"Zero observation returned a public projection peak");
    check(projection.workspace_bytes()==workspace,"Repeated projection grew workspace");
    try {
        projection.search(std::span(samples).subspan(1));
    }catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Partial proposal word was accepted");
}
}

int main() {
    try {
        check_direct_oracle();
        check_shared_geometry();
        std::cout<<"Fast SSB folded public-word projections match direct and multipath controls\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
