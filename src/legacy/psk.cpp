#include "datapump/legacy/modem.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

namespace datapump::legacy::detail {
namespace {
constexpr double pi=std::numbers::pi;
using Complex=std::complex<double>;

// Wire constants, not a dictionary shared with any of the other modems.
// PSK31 specification: https://www.arrl.org/psk31-spec . The published page has
// transcription errors around Z and p; these entries and the 8-bit extension
// agree with the FLDigi PSK varicode wire table. No FLDigi implementation is used.
constexpr auto varicode=[] {
    std::array<unsigned,256> result{
        683,731,749,887,747,863,751,765,767,239,29,879,733,31,885,939,
        759,757,941,943,859,875,877,855,891,893,951,853,861,955,763,895,
        1,511,351,501,475,725,699,383,251,247,367,479,117,53,87,431,
        183,189,237,255,375,347,363,429,427,439,245,445,493,85,471,687,
        701,125,235,173,181,119,219,253,341,127,509,381,215,187,221,171,
        213,477,175,111,109,343,437,349,373,379,685,503,495,507,703,365,
        735,11,95,47,45,3,61,91,43,13,491,191,27,59,15,7,
        63,447,21,23,5,55,123,107,223,93,469,695,443,693,727,949
    };
    // The extension enumerates the remaining no-double-zero, odd binary words
    // in increasing numerical order, beginning at 1110111101 (byte 128).
    unsigned candidate=957;
    for(std::size_t i=128;i<result.size();++i) {
        for(;;candidate+=2) {
            bool valid=true;
            for(unsigned word=candidate;word>1;word>>=1)
                if((word&3u)==0)valid=false;
            if(valid)break;
        }
        result[i]=candidate;candidate+=2;
    }
    return result;
}();

std::size_t symbol_samples(Mode mode) {return mode==Mode::bpsk125?64:256;}

class PskTransmitter final:public WaveTransmitter {
public:
    PskTransmitter(Config config,std::string text,TextCallback sent)
        :text_(std::move(text)),sent_(std::move(sent)),period_(symbol_samples(config.mode)),
         idle_symbols_(8192/period_),sample_(period_),step_(2*pi*config.carrier_hz/sample_rate) {}
    std::size_t read(std::span<float> out) override {
        std::size_t written=0;
        while(written<out.size()) {
            if(sample_==period_) {
                if(character_finished_) {
                    if(sent_)sent_(std::string_view(text_).substr(character_,1));
                    ++character_;character_finished_=false;
                }
                if(!next_symbol())break;
                sample_=0;
            }
            double envelope=previous_+(target_-previous_)*(.5-.5*std::cos(pi*sample_/period_));
            // A raised-cosine edge prevents keying clicks at the start and end.
            if(symbol_==1)envelope*=.5-.5*std::cos(pi*sample_/period_);
            if(last_symbol_)envelope*=.5+.5*std::cos(pi*sample_/period_);
            out[written++]=static_cast<float>(.8*envelope*std::cos(phase_));
            phase_+=step_;if(phase_>=2*pi)phase_-=2*pi;
            ++sample_;
            if(sample_==period_&&character_finished_) {
                if(sent_)sent_(std::string_view(text_).substr(character_,1));
                ++character_;character_finished_=false;
            }
        }
        return written;
    }
private:
    bool next_symbol() {
        bool bit=false;
        if(symbol_<idle_symbols_) {}
        else if(character_<text_.size()) {
            if(bit_position_==0) {
                word_=varicode[static_cast<unsigned char>(text_[character_])];
                code_length_=std::bit_width(word_);
            }
            bit=bit_position_<code_length_&&((word_>>(code_length_-1-bit_position_))&1u);
            if(++bit_position_==code_length_+2) {bit_position_=0;character_finished_=true;}
        } else {
            if(trailer_==idle_symbols_)return false;
            bit=true;last_symbol_=++trailer_==idle_symbols_;
        }
        previous_=target_;if(!bit)target_=-target_;++symbol_;
        return true;
    }
    std::string text_;TextCallback sent_;
    std::size_t period_,idle_symbols_,sample_,symbol_=0,character_=0,trailer_=0;
    unsigned word_=0,code_length_=0,bit_position_=0;
    double step_,phase_=0,previous_=1,target_=1;
    bool character_finished_=false,last_symbol_=false;
};

class PskReceiver final:public WaveReceiver {
public:
    PskReceiver(Config config,TextCallback callback)
        :callback_(std::move(callback)),period_(symbol_samples(config.mode)),
         decimation_(period_/16),window_(period_/2),step_(2*pi*config.carrier_hz/sample_rate) {}
    void push(std::span<const float> pcm) override {
        for(const auto sample:pcm) {
            // Half-symbol integrate-and-dump, evaluated at sixteen clock phases.
            // Differential decisions remove the unknown receive carrier phase.
            const auto finite=std::isfinite(sample)?static_cast<double>(sample):0.;
            const Complex mixed=finite*Complex(std::cos(phase_),-std::sin(phase_));
            phase_+=step_;if(phase_>=2*pi)phase_-=2*pi;
            sum_+=mixed-history_[position_];history_[position_]=mixed;
            position_=(position_+1)%window_;
            if(++samples_%decimation_==0)observe(sum_/static_cast<double>(window_));
        }
    }
private:
    struct ClockCandidate {
        Complex previous{};
        double power=0;
        unsigned reversals=0,coherent_symbols=0;
        std::uint32_t transitions=0;
    };
    void observe(Complex value) {
        const auto phase=static_cast<unsigned>(ticks_%16);
        auto& candidate=clocks_[phase];
        const double power=std::norm(value);
        const double old_power=std::norm(candidate.previous);
        const auto difference=value*std::conj(candidate.previous);
        const double scale=std::sqrt(power*old_power);
        const double confidence=scale>1e-18?difference.real()/scale:0;
        candidate.power+=.08*(power-candidate.power);
        candidate.reversals=(confidence<-.7&&power>1e-12)?std::min(32u,candidate.reversals+1):0;
        candidate.coherent_symbols=(std::abs(confidence)>.9&&power>1e-12)?
            std::min(24u,candidate.coherent_symbols+1):0;
        candidate.transitions=((candidate.transitions<<1)|static_cast<unsigned>(confidence<0))&0xffffffu;
        candidate.previous=value;
        if(!locked_&&ticks_>=16*20) {
            unsigned best=0;
            double minimum=clocks_[0].power;
            for(unsigned i=1;i<16;++i) {
                if(clocks_[i].power>clocks_[best].power)best=i;
                minimum=std::min(minimum,clocks_[i].power);
            }
            // A radio receiver can join ongoing text after its idle preamble.
            // Require 24 strongly coherent differential decisions, both phase
            // changes and holds, and modulation with an observable symbol
            // clock. Fixed-size histories provide no source-text evidence.
            const auto changes=std::popcount(candidate.transitions);
            const bool ongoing=candidate.coherent_symbols>=24&&changes>=2&&changes<=22&&
                minimum<.9*candidate.power;
            if(phase==best&&(candidate.reversals>=16||ongoing)) {
                locked_=true;previous_=value;next_tick_=ticks_+16;weak_symbols_=0;steady_symbols_=0;
                word_=0;bits_=0;zero_=false;overlong_=false;
                character_sync_=candidate.reversals>=16;
            }
        } else if(locked_&&ticks_==next_tick_) {
            const double previous_power=std::norm(previous_);
            const double norm=std::sqrt(power*previous_power);
            const double decision=norm>1e-18?(value*std::conj(previous_)).real()/norm:0;
            const bool present=power>1e-12&&std::abs(decision)>.55;
            previous_=value;next_tick_+=16;
            if(!present) {
                word_=0;bits_=0;zero_=false;overlong_=false;character_sync_=false;
                weak_symbols_+=2;
                if(weak_symbols_>=12)locked_=false;
            } else {
                if(weak_symbols_)--weak_symbols_;
                steady_symbols_=decision>0?steady_symbols_+1:0;
                // Standard PSK postamble is a steady carrier. No valid 8-bit
                // varicode word can contain this many uninterrupted one bits.
                if(steady_symbols_>=24)locked_=false;
                else decode(decision>0);
            }
            // Very slow phase tracking follows ordinary sound-card clock error.
            // No timing updates during an unmodulated postamble.
            if(locked_&&++track_symbols_%16==0) {
                unsigned best=phase;
                double minimum=candidate.power;
                for(unsigned i=0;i<16;++i) {
                    if(clocks_[i].power>clocks_[best].power)best=i;
                    minimum=std::min(minimum,clocks_[i].power);
                }
                if(minimum<.75*clocks_[best].power) {
                    const int delta=(static_cast<int>(best)-static_cast<int>(phase)+24)%16-8;
                    if(delta>0)++next_tick_;else if(delta<0)--next_tick_;
                }
            }
        }
        ++ticks_;
    }
    void decode(bool bit) {
        if(!character_sync_) {
            // A joined stream may begin halfway through a valid codeword. Its
            // suffix is not a character: wait for an observed 00 delimiter.
            if(!bit&&zero_) {character_sync_=true;zero_=false;}
            else zero_=!bit;
            return;
        }
        if(!bit&&zero_) {
            // The pending zero belongs to the delimiter, never to the code.
            if(bits_&&!overlong_) {
                const auto found=std::find(varicode.begin(),varicode.end(),word_);
                if(found!=varicode.end()) {
                    const char character=static_cast<char>(found-varicode.begin());
                    if(callback_)callback_(std::string_view(&character,1));
                }
            }
            word_=0;bits_=0;overlong_=false;zero_=false;
        } else if(bit) {
            if(zero_&&bits_) {word_<<=1;++bits_;}
            zero_=false;
            if(bits_<12) {word_=(word_<<1)|1u;++bits_;}
            else overlong_=true;
        } else zero_=true;
    }
    TextCallback callback_;
    std::size_t period_,decimation_,window_,position_=0;
    std::array<Complex,128> history_{};
    std::array<ClockCandidate,16> clocks_{};
    Complex sum_{},previous_{};
    double step_,phase_=0;
    std::uint64_t samples_=0,ticks_=0,next_tick_=0;
    unsigned weak_symbols_=0,steady_symbols_=0,track_symbols_=0,word_=0,bits_=0;
    bool locked_=false,zero_=false,overlong_=false,character_sync_=false;
};
}

std::unique_ptr<WaveTransmitter> psk_transmitter(Config config,std::string text,TextCallback sent) {
    return std::make_unique<PskTransmitter>(config,std::move(text),std::move(sent));
}
std::unique_ptr<WaveReceiver> psk_receiver(Config config,TextCallback received) {
    return std::make_unique<PskReceiver>(config,std::move(received));
}
}
