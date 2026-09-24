#include "datapump/legacy/modem.hpp"
#include "../src/legacy/olivia_codec.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace datapump::legacy;
namespace {
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
constexpr Config configuration{Mode::olivia4_2000,1500};
std::string decode(std::span<const float> pcm,std::size_t chunk,Config config=configuration) {
    std::string result;
    auto receiver=detail::olivia_receiver(config,[&](auto text){result+=text;});
    for(std::size_t i=0;i<pcm.size();i+=chunk)
        receiver->push(pcm.subspan(i,std::min(chunk,pcm.size()-i)));
    return result;
}
std::vector<float> encode(std::string text,std::size_t chunk,Config config=configuration) {
    std::string echo;
    auto transmitter=detail::olivia_transmitter(config,text,[&](auto characters){echo+=characters;});
    require(echo.empty(),"transmit echo arrived before PCM was emitted");
    std::vector<float> result,buffer(chunk);
    while(auto count=transmitter->read(buffer))
        result.insert(result.end(),buffer.begin(),buffer.begin()+static_cast<std::ptrdiff_t>(count));
    require(echo==text,"transmit echo omitted or reordered text");
    return result;
}
std::vector<float> fixture() {
    const auto path=std::filesystem::path(__FILE__).parent_path()/"fixtures/legacy/olivia4-2000-fldigi.s16le";
    std::ifstream file(path,std::ios::binary);
    require(bool(file),"independent FLDigi PCM fixture missing");
    std::vector<float> pcm;
    std::array<unsigned char,2> bytes{};
    while(file.read(reinterpret_cast<char*>(bytes.data()),2)) {
        const unsigned bits=unsigned(bytes[0])|(unsigned(bytes[1])<<8);
        const int value=bits<32768?int(bits):int(bits)-65536;
        pcm.push_back(static_cast<float>(value/32760.));
    }
    return pcm;
}
}
int main() {try {
    // Independent tone numbers captured from upstream FLDigi's encoder, pinned
    // and reproduced as described alongside the PCM fixture.
    const std::array<std::pair<std::array<unsigned char,2>,std::string_view>,2> vectors{{
        {{{'C','Q'}},"1121222120223313311201030031230211020121222313203322002223321310"},
        {{{0,0}},"3112021200111320201010011133320031312112021033132220112032300212"}
    }};
    for(const auto& [characters,expected]:vectors) {
        const auto tones=detail::olivia::encode(characters);
        for(std::size_t i=0;i<64;++i) require(tones[i]==expected[i]-'0',"independent Olivia wire vector changed");
    }
    auto independent=fixture();
    const std::string expected="CQ CQ TEST 123 Hello world\n";
    for(const std::size_t chunk:{1U,17U,513U})
        require(decode(independent,chunk)==expected,"upstream FLDigi PCM failed to decode");
    const std::string message="CQ de TEST 123\nUTF-8: \xc3\xa9 \xe2\x98\x83\n";
    auto pcm=encode(message,1);
    require(pcm==encode(message,3079),"Olivia transmit waveform depends on read chunk");
    for(unsigned offset=0;offset<16;++offset) {
        auto delayed=pcm;delayed.insert(delayed.begin(),offset,0);
        require(decode(delayed,193)==message,"Olivia sample-phase acquisition failed");
    }
    auto shifted=configuration;shifted.carrier_hz=1637.5;
    require(decode(encode(message,91,shifted),73,shifted)==message,"editable Olivia carrier failed");
    auto detuned=configuration;detuned.carrier_hz+=45;
    require(decode(pcm,91,detuned)==message,"modest carrier mismatch failed");
    std::mt19937 random(92491);
    std::normal_distribution<float> noise(0,0.025F);
    for(auto& sample:independent) sample=sample*0.2F+noise(random);
    require(decode(independent,127)==expected,"independent noisy FLDigi PCM failed");
    std::vector<float> silence(8000*2);
    require(decode(silence,93).empty(),"silence generated Olivia text");
    for(auto& sample:silence)sample=noise(random);
    require(decode(silence,137).empty(),"noise generated Olivia text");
    // Stream text arrives while transmission continues; the receiver has no
    // finish/EOF operation and cannot require one to expose characters.
    std::string received;
    auto receiver=detail::olivia_receiver(configuration,[&](auto text){received+=text;});
    receiver->push(std::span<const float>(pcm).first(8192+1024+128));
    require(!received.empty() && received.size()<message.size(),"Olivia text waited for the full transmission");
    // A completed character must not be echoed before its entire shaped pulse.
    std::string sent;
    auto transmitter=detail::olivia_transmitter(configuration,"ABCD",[&](auto text){sent+=text;});
    std::vector<float> first(8192+1024+15);
    require(transmitter->read(first)==first.size() && sent.empty(),"Olivia character echoed before final pulse");
    std::array<float,1> last{};transmitter->read(last);
    require(sent=="AB","Olivia completed characters not echoed promptly");
    std::cout<<"legacy Olivia: independent vectors, FLDigi PCM, streaming and noise passed\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
