#include "ssb_folded_preamble.hpp"
#include "datapump/execution.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace datapump::fast::detail {
namespace {
namespace fft = modem::detail::search_fft;
using Complex = SsbFoldedPreamble::Complex;
constexpr auto tau = 2 * std::numbers::pi;
constexpr std::size_t max_word_samples = 1024;
constexpr std::size_t max_words = 32;
constexpr int max_frequency_half_bins = 1024;

std::size_t transform_size(std::size_t word_samples) {
    if(word_samples<2 || word_samples>max_word_samples)
        throw std::invalid_argument("Fast SSB proposal word is outside its FFT geometry");
    return std::bit_ceil(2*word_samples-1);
}

double energy(std::span<const Complex> samples) {
    double sum=0.;
    for(const auto sample:samples)sum+=std::norm(sample);
    return sum;
}
}

SsbFoldedPreamble::SsbFoldedPreamble(std::span<const Complex> reference_word,
                                    double proposal_sample_rate,double max_abs_doppler_hz)
    : sample_rate_(proposal_sample_rate),max_doppler_(max_abs_doppler_hz),
      fft_(transform_size(reference_word.size())),reference_energy_(energy(reference_word)),
      reference_spectrum_(fft_.maximum),folded_(reference_word.size()),spectrum_(fft_.maximum) {
    if(!std::isfinite(sample_rate_) || sample_rate_<=0 ||
       !std::isfinite(max_doppler_) || max_doppler_<0 || max_doppler_>=sample_rate_/2 ||
       !std::isfinite(reference_energy_) || reference_energy_<=0)
        throw std::invalid_argument("Invalid Fast SSB proposal reference or Doppler range");
    for(std::size_t i=0;i<reference_word.size();++i)
        reference_spectrum_[i]=std::conj(reference_word[reference_word.size()-1-i]);
    fft::fft(reference_spectrum_,false,fft_);
}

std::optional<SsbFoldedPeak> SsbFoldedPreamble::search(std::span<const Complex> samples,
                                                      std::stop_token stop) {
    const auto word=folded_.size();
    const auto words=samples.size()/word;
    if(!words || words>max_words || samples.size()%word)
        throw std::invalid_argument("Fast SSB projection requires complete public words");
    const auto bin_hz=sample_rate_/static_cast<double>(samples.size());
    const auto half_bins=std::floor(max_doppler_/bin_hz);
    if(!std::isfinite(half_bins) || half_bins>max_frequency_half_bins)
        throw std::invalid_argument("Fast SSB proposal exceeds its Doppler-bin budget");
    if(stop.stop_requested())return std::nullopt;
    const auto observed_energy=energy(samples);
    if(!std::isfinite(observed_energy) || observed_energy<=0)return std::nullopt;

    const auto edge=static_cast<int>(half_bins);
    const auto repeats=static_cast<int>(words);
    SsbFoldedPeak best{};
    double best_power=-1.;
    try {
        for(int residue=0;residue<repeats;++residue) {
            if(stop.stop_requested())return std::nullopt;
            const auto first_bin=residue-(edge+residue)/repeats*repeats;
            if(first_bin>edge)continue;
            std::fill(folded_.begin(),folded_.end(),Complex{});
            const auto block_step=std::polar(1.,-tau*residue/repeats);
            Complex block_phase=1.;
            for(std::size_t block=0;block<words;++block) {
                for(std::size_t j=0;j<word;++j)
                    folded_[j]+=samples[block*word+j]*block_phase;
                block_phase*=block_step;
            }

            for(int bin=first_bin;bin<=edge;bin+=repeats) {
                if(stop.stop_requested())return std::nullopt;
                const auto step=std::polar(1.,-tau*bin/static_cast<double>(samples.size()));
                Complex phase=1.;
                for(std::size_t j=0;j<word;++j) {
                    spectrum_[j]=folded_[j]*phase;
                    phase*=step;
                }
                std::fill(spectrum_.begin()+static_cast<std::ptrdiff_t>(word),spectrum_.end(),Complex{});
                fft::fft(spectrum_,false,fft_,stop);
                for(std::size_t j=0;j<spectrum_.size();++j)spectrum_[j]*=reference_spectrum_[j];
                fft::fft(spectrum_,true,fft_,stop);
                for(std::size_t delay=0;delay<word;++delay) {
                    // The two sections of the linear correlation form the
                    // circular correlation at this public word phase.
                    const auto correlation=spectrum_[word-1+delay]+(delay?spectrum_[delay-1]:Complex{});
                    const auto power=std::norm(correlation);
                    if(power>best_power) {
                        best_power=power;
                        best.delay_samples=delay;
                        best.frequency_hz=bin*bin_hz;
                    }
                }
                execution::checkpoint();
            }
        }
    }catch(const Error&) {
        if(stop.stop_requested())return std::nullopt;
        throw;
    }
    const auto coherence=(best_power/observed_energy)/(static_cast<double>(words)*reference_energy_);
    if(!std::isfinite(coherence) || coherence<0)return std::nullopt;
    best.coherence=std::min(1.,coherence);
    return best;
}

std::size_t SsbFoldedPreamble::workspace_bytes() const noexcept {
    return sizeof(*this)+fft_.working_bytes()+
        (reference_spectrum_.capacity()+folded_.capacity()+spectrum_.capacity())*sizeof(Complex);
}

} // namespace datapump::fast::detail
