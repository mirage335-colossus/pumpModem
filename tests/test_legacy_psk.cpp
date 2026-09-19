#include "datapump/legacy/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace datapump::legacy;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
std::size_t period(Config c) {return c.mode==Mode::bpsk125?64:256;}
std::vector<float> transmit(Config c,const std::string& text,std::size_t chunk=173) {
    std::string sent;
    auto tx=detail::psk_transmitter(c,text,[&](std::string_view s){sent+=s;});
    require(sent.empty(),"TX callback precedes waveform emission");
    std::vector<float> out,buffer(chunk);
    for(;;) {
        const auto count=tx->read(buffer);
        if(!count)break;
        for(std::size_t i=0;i<count;++i)out.push_back(buffer[i]);
    }
    require(sent==text,"TX character callbacks do not match source");
    return out;
}
std::string receive(Config c,std::span<const float> pcm,std::size_t chunk=191) {
    std::string text;
    auto rx=detail::psk_receiver(c,[&](std::string_view s){text+=s;});
    for(std::size_t at=0;at<pcm.size();at+=chunk)
        rx->push(pcm.subspan(at,std::min(chunk,pcm.size()-at)));
    return text;
}

std::vector<float> fldigi_fixture(Mode mode) {
    const auto filename=mode==Mode::bpsk31?"bpsk31-fldigi-4.2.06.s16le":"bpsk125-fldigi-4.2.06.s16le";
    const auto path=std::filesystem::path(__FILE__).parent_path()/"fixtures/legacy"/filename;
    std::ifstream file(path,std::ios::binary);
    require(bool(file),"independent FLDigi PSK PCM fixture missing");
    std::vector<float> pcm;
    char bytes[2]{};
    while(file.read(bytes,2)) {
        const unsigned bits=static_cast<unsigned char>(bytes[0])|
            (static_cast<unsigned>(static_cast<unsigned char>(bytes[1]))<<8);
        const int value=bits<32768?static_cast<int>(bits):static_cast<int>(bits)-65536;
        pcm.push_back(static_cast<float>(value/32760.));
    }
    require(file.eof()&&file.gcount()==0,"independent FLDigi PSK PCM fixture truncated");
    require(pcm.size()==(mode==Mode::bpsk31?69632:31232),"independent FLDigi PSK PCM fixture length differs");
    return pcm;
}

// Independent sampled fixture assembled from published wire bits. Its carrier
// phase, amplitude, preamble length, and leading fractional symbol differ from
// our transmitter. No encoder, varicode lookup, or TX DSP is called here.
std::vector<float> external_wave(Config c,std::string_view bits,double frequency_offset=0) {
    const std::size_t n=period(c),leading=117;
    const auto wire=std::string(48,'0')+std::string(bits)+std::string(40,'1');
    std::vector<float> out(leading+wire.size()*n+2000);
    double sign=1;
    for(std::size_t symbol=0;symbol<wire.size();++symbol) {
        for(std::size_t i=0;i<n;++i) {
            const double envelope=wire[symbol]=='0'?sign*std::cos(std::numbers::pi*i/n):sign;
            const double time=static_cast<double>(symbol*n+i)/sample_rate;
            out[leading+symbol*n+i]=static_cast<float>(.23*envelope*
                std::cos(2*std::numbers::pi*(c.carrier_hz+frequency_offset)*time+.73));
        }
        if(wire[symbol]=='0')sign=-sign;
    }
    return out;
}

void vectors(Config c) {
    const std::string known="e aZp\n%";
    // e=11, space=1, a=1011, Z=1010101101, p=111111, LF=11101,
    // percent=1011010101. Includes two ARRL web-table transcription traps.
    const std::string bits="1100" "100" "101100" "101010110100" "11111100" "1110100" "101101010100";
    const auto n=period(c);
    const auto pcm=transmit(c,known);
    require(pcm.size()==16384+bits.size()*n,"PSK wire duration differs from standard varicode");
    require(pcm==transmit(c,known,1),"PSK TX changes with read chunk sizes");
    // Independently extract the sign at every symbol boundary. The waveform is
    // at its prior phase at that boundary; the next boundary exposes this bit.
    double sign=1;
    for(std::size_t bit=0;bit<bits.size();++bit) {
        if(bits[bit]=='0')sign=-sign;
        const std::size_t sample=8192+(bit+1)*n;
        const double carrier=std::cos(2*std::numbers::pi*c.carrier_hz*sample/sample_rate);
        require(std::abs(pcm[sample]-.8*sign*carrier)<1e-5,"independent PSK wire bit vector differs");
    }
    require(receive(c,external_wave(c,bits),1)==known,"independent cosine waveform decode failed");
    require(receive(c,external_wave(c,bits),509)==known,"external waveform chunk dependence");
    require(receive(c,external_wave(c,bits,1.25))==known,"small carrier mismatch decode failed");

    std::string sent;
    auto tx=detail::psk_transmitter(c,"ee",[&](std::string_view s){sent+=s;});
    std::vector<float> first(8192+4*n-1);
    require(tx->read(first)==first.size()&&sent.empty(),"partial character reported sent");
    float final{};require(tx->read(std::span<float>(&final,1))==1&&sent=="e","character not exposed when its final sample emitted");
    // RX emits the first character while the following characters are in flight.
    const auto long_pcm=transmit(c,"eeeeeeeeeeeeeeee");
    std::string early;
    auto rx=detail::psk_receiver(c,[&](std::string_view s){early+=s;});
    rx->push(std::span<const float>(long_pcm).first(8192+6*n));
    require(early=="e","RX delayed first text until whole transmission");
}

void joined_reference(Config c,const std::vector<float>& pcm) {
    const std::string text="CQ de N0CALL Test 123 % Z p";
    const auto n=period(c);
    for(const std::size_t symbols:{20u,55u,93u,120u}) {
        for(const auto offset:std::array<std::size_t,4>{0,1,13,n/2+3}) {
            // Entire preamble and multiple characters are absent; the capture
            // starts inside an arbitrary symbol/codeword of the external TX.
            const auto part=std::span<const float>(pcm).subspan(8192+symbols*n+offset);
            const auto output=receive(c,part,1);
            require(output.size()>=std::string_view("% Z p").size()&&text.ends_with(output),
                    "midstream FLDigi join omitted suffix or exposed a partial codeword");
            require(output==receive(c,part,509),"midstream acquisition depends on push chunks");
        }
    }
    // Joining also works after a long silence. Acquisition uses local bounded
    // symbol histories rather than elapsed stream position or a buffered file.
    std::string output;
    auto receiver=detail::psk_receiver(c,[&](std::string_view s){output+=s;});
    const std::vector<float> silence(sample_rate*3);
    receiver->push(silence);
    receiver->push(std::span<const float>(pcm).subspan(8192+55*n+17));
    require(output.size()>=std::string_view("123 % Z p").size()&&text.ends_with(output),
            "receiver joining ongoing text after silence failed");
}

void long_noise_rejection(Config c) {
    for(const unsigned seed:{173u,409u,881u,1361u}) {
        std::string output;
        auto receiver=detail::psk_receiver(c,[&](std::string_view s){output+=s;});
        std::mt19937 random(seed);
        std::normal_distribution<float> noise(0,.08f);
        std::vector<float> block(4093);
        std::size_t remaining=sample_rate*120;
        while(remaining) {
            const auto count=std::min(remaining,block.size());
            for(std::size_t i=0;i<count;++i)block[i]=noise(random);
            receiver->push(std::span<const float>(block).first(count));
            remaining-=count;
        }
        require(output.empty(),"long noise-only stream opened PSK character squelch");
    }
    // A coherent unmodulated carrier is not evidence of mixed PSK text.
    std::vector<float> carrier(sample_rate*3);
    for(std::size_t i=0;i<carrier.size();++i)
        carrier[i]=static_cast<float>(.2*std::cos(2*std::numbers::pi*c.carrier_hz*i/sample_rate));
    require(receive(c,carrier).empty(),"steady carrier manufactured PSK text");
}
}

int main() {try {
    for(const auto mode:{Mode::bpsk31,Mode::bpsk125}) {
        Config c{mode,1500};vectors(c);
        // Captured from the installed FLDigi binary through a fake audio sink.
        // Its independent transmitter and encoding are never invoked at test
        // runtime; see fixtures/legacy/PSK-fixtures.md for provenance.
        auto reference=fldigi_fixture(mode);
        for(const std::size_t chunk:{1u,97u,509u})
            require(receive(c,reference,chunk)=="CQ de N0CALL Test 123 % Z p","installed FLDigi PSK fixture decode failed");
        joined_reference(c,reference);
        long_noise_rejection(c);
        reference.insert(reference.begin(),137,0);
        std::mt19937 reference_random(479);
        std::normal_distribution<float> reference_noise(0,.015f);
        for(auto& value:reference)value=.2f*value+reference_noise(reference_random);
        require(receive(c,reference,113)=="CQ de N0CALL Test 123 % Z p","noisy delayed independent FLDigi PSK fixture decode failed");
        joined_reference(c,reference);
        std::string alphabet;
        for(unsigned i=0;i<256;++i)alphabet+=static_cast<char>(i);
        auto pcm=transmit(c,alphabet);
        // Independent all-byte wire fingerprint computed from FLDigi's public
        // PSK varicode table (256 words, each followed by 00). Hash the observed
        // waveform phase decisions, not the production encoder's table.
        require(pcm.size()==16384+2988*period(c),"all-byte reference wire length differs");
        std::uint64_t hash=14695981039346656037ull;
        bool prior_positive=true;
        for(std::size_t i=0;i<2988;++i) {
            const bool positive=pcm[8192+(i+1)*period(c)]>0;
            hash^=static_cast<unsigned>(positive==prior_positive);
            hash*=1099511628211ull;prior_positive=positive;
        }
        require(hash==0xb14bfba15fe6b322ull,"all-byte independent varicode fingerprint differs");
        require(receive(c,pcm)==alphabet,"256-byte PSK varicode round trip failed");
        const std::string message="CQ CQ de N0CALL 73\nThe quick brown fox jumps over the lazy dog.";
        for(const auto carrier:{537.25,1500.,2873.5}) {
            c.carrier_hz=carrier;
            const auto clean=transmit(c,message);
            for(const auto leading:{0,1,29,137,263}) {
                auto noisy=clean;noisy.insert(noisy.begin(),static_cast<std::size_t>(leading),0);
                std::mt19937 engine(901+leading);
                std::normal_distribution<float> noise(0,.018f);
                for(auto& value:noisy)value=.16f*value+noise(engine);
                noisy.resize(noisy.size()+4000,0);
                const auto decoded=receive(c,noisy,97);
                if(decoded!=message)std::cerr<<"mode "<<static_cast<int>(mode)<<" carrier "<<carrier<<" leading "<<leading<<" decoded "<<decoded<<'\n';
                require(decoded==message,"amplitude/noise/start-offset regression failed");
            }
        }
        c.carrier_hz=1500;
        std::mt19937 engine(7241);std::normal_distribution<float> noise(0,.08f);
        std::vector<float> nothing(sample_rate*20);
        require(receive(c,nothing).empty(),"silence manufactured PSK text");
        for(auto& value:nothing)value=noise(engine);
        require(receive(c,nothing).empty(),"noise opened PSK squelch");
        // One receiver must return to idle and acquire another arbitrary start.
        auto second=transmit(c,"second");pcm=transmit(c,"first");
        pcm.insert(pcm.end(),nothing.begin(),nothing.end());
        pcm.insert(pcm.end(),second.begin(),second.end());
        require(receive(c,pcm)=="firstsecond","receiver failed to reacquire after noise");
    }
    std::cout<<"legacy PSK regressions passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
