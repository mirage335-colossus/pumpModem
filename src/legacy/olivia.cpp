#include "datapump/legacy/modem.hpp"
#include "olivia_codec.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <numbers>
#include <optional>
#include <utility>

namespace datapump::legacy::detail {
namespace {
constexpr std::size_t symbol_samples=16, pulse_samples=32, block_samples=1024;
constexpr double tau=2*std::numbers::pi;
constexpr unsigned idle_blocks=8;

struct Character {unsigned char code=0;std::string echo;};
class OliviaTransmitter final:public WaveTransmitter {
    TextCallback sent_;
    std::vector<Character> characters_;
    std::size_t source_=0,block_=0,symbol_=64,position_=16,emitted_=0;
    std::size_t blocks_=0;
    std::deque<std::pair<std::size_t,std::string>> echoes_;
    olivia::Tones tones_{};
    std::array<double,32> overlap_{};
    std::array<float,16> output_{};
    std::array<double,4> omega_{};
    double phase_=0;
    std::uint32_t random_=0x6d2b79f5U;
    bool tail_done_=false;
    void next_block() {
        std::array<unsigned char,2> bytes{};
        if(block_>=idle_blocks && block_<blocks_-idle_blocks) {
            std::string echo;
            for(auto& byte:bytes) if(source_<characters_.size()) {
                const auto& character=characters_[source_++];
                byte=character.code;
                echo+=character.echo;
            }
            echoes_.emplace_back((block_+1)*block_samples+symbol_samples,std::move(echo));
        }
        tones_=olivia::encode(bytes);symbol_=0;++block_;
    }
    bool fill() {
        if(symbol_==64) {
            if(block_==blocks_) {
                if(tail_done_) return false;
                tail_done_=true;
                for(std::size_t i=0;i<16;++i) output_[i]=static_cast<float>(overlap_[i]);
                position_=0;return true;
            }
            next_block();
        }
        const double omega=omega_[tones_[symbol_++]];
        const double start=phase_-omega*8;
        for(std::size_t i=0;i<32;++i) {
            const double shape=0.25*(1-std::cos(tau*static_cast<double>(i)/32));
            overlap_[i]+=shape*std::cos(start+omega*static_cast<double>(i));
        }
        random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
        phase_=std::remainder(phase_+omega*16+((random_&1U)?tau/4:-tau/4),tau);
        for(std::size_t i=0;i<16;++i) {
            output_[i]=static_cast<float>(overlap_[i]);
            overlap_[i]=overlap_[i+16];overlap_[i+16]=0;
        }
        position_=0;return true;
    }
public:
    OliviaTransmitter(Config config,std::string text,TextCallback sent):sent_(std::move(sent)) {
        for(unsigned char byte:text) {
            if(byte>=128) characters_.push_back({127,{}});
            characters_.push_back({static_cast<unsigned char>(byte&127U),std::string(1,static_cast<char>(byte))});
        }
        blocks_=2*idle_blocks+(characters_.size()+1)/2;
        for(std::size_t tone=0;tone<4;++tone)
            omega_[tone]=tau*(config.carrier_hz-750+500*static_cast<double>(tone))/sample_rate;
    }
    std::size_t read(std::span<float> output) override {
        std::size_t n=0;
        while(n<output.size()) {
            if(position_==16 && !fill()) break;
            output[n++]=output_[position_++];++emitted_;
            if(!echoes_.empty() && emitted_==echoes_.front().first) {
                if(sent_&&!echoes_.front().second.empty()) sent_(echoes_.front().second);
                echoes_.pop_front();
            }
        }
        return n;
    }
};

class OliviaReceiver final:public WaveReceiver {
    struct Phase {
        std::array<std::array<double,2>,64> bits{};
        std::array<double,64> energy{};
        std::size_t count=0;
    };
    struct Candidate {olivia::Decoded decoded;std::size_t end=0;};
    TextCallback received_;
    double squelch_=.72;
    std::array<float,32> samples_{};
    std::array<std::array<double,32>,4> cosine_{},sine_{};
    std::array<Phase,4> phases_{};
    std::size_t total_=0,last_end_=0;
    std::optional<Candidate> candidate_;
    bool escape_=false;
    void emit(const Candidate& candidate) {
        last_end_=candidate.end;
        for(unsigned char byte:candidate.decoded.characters) {
            if(escape_) {byte=static_cast<unsigned char>(byte+128);escape_=false;}
            else if(byte==127) {escape_=true;continue;}
            else if(byte<8) continue; // Olivia's idle NUL and reserved controls.
            const char character=static_cast<char>(byte);
            if(received_) received_(std::string_view(&character,1));
        }
    }
    void score() {
        auto& phase=phases_[(total_/4)%4];
        std::array<double,4> power{};
        for(std::size_t tone=0;tone<4;++tone) {
            double real=0,imaginary=0;
            for(std::size_t i=0;i<32;++i) {
                const auto sample=samples_[(total_+i)%32];
                real+=sample*cosine_[tone][i];imaginary+=sample*sine_[tone][i];
            }
            power[tone]=real*real+imaginary*imaginary;
        }
        const double energy=power[0]+power[1]+power[2]+power[3];
        const auto slot=phase.count++%64;
        phase.energy[slot]=energy;
        // Tone numbers are Gray coded: tones 0,1,2,3 represent 00,01,11,10.
        phase.bits[slot]={(power[0]-power[1]-power[2]+power[3])/(energy+1e-30),
                          (power[0]+power[1]-power[2]-power[3])/(energy+1e-30)};
        if(phase.count<64 || (last_end_ && total_<last_end_+block_samples-8)) return;
        std::array<std::array<double,2>,64> ordered{};
        double accumulated_energy=0;
        for(std::size_t t=0;t<64;++t) {
            ordered[t]=phase.bits[(phase.count+t)%64];
            accumulated_energy+=phase.energy[(phase.count+t)%64];
        }
        if(accumulated_energy<1e-8) return;
        const auto decoded=olivia::decode(ordered);
        // Two independent, scrambled Walsh lanes must both agree. No source
        // syntax or packet metadata participates in symbol/block acquisition.
        if(decoded.confidence<squelch_) return;
        if(!candidate_ || decoded.confidence>candidate_->decoded.confidence)
            candidate_=Candidate{decoded,total_};
    }
public:
    OliviaReceiver(Config config,TextCallback received):received_(std::move(received)),squelch_(config.squelch==0?.60:config.squelch==1?.72:.85) {
        for(std::size_t tone=0;tone<4;++tone) {
            const double omega=tau*(config.carrier_hz-750+500*static_cast<double>(tone))/sample_rate;
            for(std::size_t i=0;i<32;++i) {
                const double window=1-std::cos(tau*static_cast<double>(i)/32);
                cosine_[tone][i]=window*std::cos(omega*static_cast<double>(i));
                sine_[tone][i]=window*std::sin(omega*static_cast<double>(i));
            }
        }
    }
    void push(std::span<const float> input) override {
        for(const float sample:input) {
            samples_[total_%32]=std::isfinite(sample)?sample:0;++total_;
            if(total_<32 || total_%4) continue;
            score();
            if(candidate_ && total_>=candidate_->end+16) {
                emit(*candidate_);candidate_.reset();
            }
            if(last_end_ && total_>last_end_+3*block_samples) {
                last_end_=0;escape_=false;
            }
        }
    }
};
}
std::unique_ptr<WaveTransmitter> olivia_transmitter(Config config,std::string text,TextCallback sent) {
    return std::make_unique<OliviaTransmitter>(config,std::move(text),std::move(sent));
}
std::unique_ptr<WaveReceiver> olivia_receiver(Config config,TextCallback received) {
    return std::make_unique<OliviaReceiver>(config,std::move(received));
}
}
