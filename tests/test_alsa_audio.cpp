#include "datapump/audio.hpp"
#include "alsa_stub.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace a=datapump::audio;
namespace f=alsa_test;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action){try{action();}catch(const datapump::Error&){check(f::state.live==0,"leaked failed stream");return;}throw std::runtime_error("invalid audio accepted");}
int main(){try{
    f::reset();f::state.hints={{"null",""},{"default:CARD=HDMI","Output"},{"default:CARD=Generic_1",""}};
    f::state.available={"null","default:CARD=Generic_1"};
    auto captured=a::record(.1,48000,"default");
    check(f::state.attempts==std::vector<std::string>{"default","default:CARD=Generic_1"},"default must discover direction-compatible card default, never null");
    check(captured.size()==4800 && f::state.opens==1 && f::state.live==0,"capture fallback lifecycle");
    f::reset();f::state.hints={{"default:CARD=HDMI","Output"},{"default:CARD=Generic_1",""}};
    f::state.available={"default:CARD=HDMI","default:CARD=Generic_1"};
    a::record(.01,48000,"default");
    const auto input_default=f::state.selected;
    a::play(std::vector<float>(10,.1f),48000,"default");
    check(f::state.selected==input_default,"fallback must prefer the same duplex card for input and output over output-only HDMI");
    f::reset();f::state.hints={{"default:CARD=Bad",""},{"default:CARD=Good",""}};
    f::state.available={"default","default:CARD=Bad","default:CARD=Good"};
    f::state.wrong_format={"default","default:CARD=Bad"};
    a::play(std::vector<float>(100,.25f),96000,"default");
    check(f::state.selected=="default:CARD=Good" && f::state.rate==96000 && f::state.opens==3 && f::state.closes==3,"24k bandwidth sample rate must try a compatible default format");
    f::reset();f::state.hints={{"plughw:CARD=HDMI,DEV=0","Output"},{"default:CARD=Generic_1",""},{"plughw:CARD=Generic_1,DEV=0",""}};
    f::state.available={"default:CARD=Generic_1","plughw:CARD=HDMI,DEV=0","plughw:CARD=Generic_1,DEV=0"};
    f::state.wrong_format={"default:CARD=Generic_1"};
    a::play(std::vector<float>(10,.1f),96000,"default");
    check(f::state.selected=="plughw:CARD=Generic_1,DEV=0","format fallback must stay on a discovered default card with PCM conversion");
    f::reset();f::state.available={"default:CARD=Good"};f::state.hints={{"default:CARD=Good",""}};
    rejects([&]{a::play(std::vector<float>(1),48000,"explicit-bad");});
    check(f::state.attempts==std::vector<std::string>{"explicit-bad"},"explicit endpoint must not silently change");
    f::reset();f::state.available={"default"};std::size_t generated=0,callbacks=0;
    a::playback(48000,"default",[&](std::span<float> chunk){
        check(chunk.size()<=2400,"stream output chunk longer than 50ms");++callbacks;
        const auto count=std::min<std::size_t>(chunk.size(),10000-generated);
        for(std::size_t i=0;i<count;++i)chunk[i]=static_cast<float>((generated+i)%1000)/1000;
        generated+=count;return count;
    });
    check(f::state.opens==1 && callbacks>=5 && f::state.played.size()==10000,"streaming playback reopened or lost samples");
    for(std::size_t i=0;i<f::state.played.size();++i)check(f::state.played[i]==static_cast<std::int16_t>((static_cast<float>(i%1000)/1000)*32767),"partial writes reordered or regenerated source samples");
    check(f::state.live==0,"streaming playback leaked");
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){return chunk.size()+1;});});
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){chunk[0]=std::numeric_limits<float>::quiet_NaN();return 1;});});
    check(f::state.played.empty(),"invalid generated samples were played");
    std::cout<<"ALSA default resolution and streaming tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
