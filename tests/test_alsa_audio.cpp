#include "datapump/audio.hpp"
#include "alsa_stub.hpp"
#include "../src/alsa_plugin_path.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numbers>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace a=datapump::audio;
namespace f=alsa_test;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action){try{action();}catch(const datapump::Error&){check(f::state.live==0,"leaked failed stream");return;}throw std::runtime_error("invalid audio accepted");}
void plugin_directories() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("datapump-alsa-plugins-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"could not create plugin-directory fixture");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{root};
    fs::create_directories(root/"bundle/lib");
    const auto library=root/"bundle/lib/libasound.so.2";
    {std::ofstream fixture(library);fixture<<"fixture";}
    const std::vector<fs::path> hosts{root/"missing/alsa-lib",root/"host/multiarch/alsa-lib",root/"host/lib/alsa-lib"};
    fs::create_directories(hosts[1]);fs::create_directories(hosts[2]);
    const auto selected=[&](const char* configured) {return a::detail::alsa_plugin_directory(configured,library,hosts);};
    check(selected(nullptr)==hosts[1].string(),"relocated ALSA did not discover the host module directory");
    check(selected("/explicit/plugins").empty()&&selected("").empty(),"explicit ALSA_PLUGIN_DIR was overridden");
    fs::create_directory(library.parent_path()/"alsa-lib");
    check(selected(nullptr).empty(),"library-owned ALSA plugins lost precedence");
    fs::remove(library.parent_path()/"alsa-lib");fs::remove_all(root/"host");
    check(selected(nullptr).empty(),"missing host plugins manufactured a module directory");
}
void selected_endpoint() {
    // A missing/busy shared route must not quietly reserve a different card.
    for(const auto& requested:std::vector<std::string>{"default",""}) {
        f::reset();f::state.hints={{"null",""},{"default:CARD=HDMI","Output"},
            {"default:CARD=Generic_1",""},{"sysdefault:CARD=Generic_1",""},{"plughw:CARD=Generic_1,DEV=0",""}};
        f::state.available={"null","default:CARD=Generic_1","sysdefault:CARD=Generic_1","plughw:CARD=Generic_1,DEV=0"};
        f::state.open_errors={{"default",-EBUSY}};
        std::string error;
        try {a::record(.1,48000,requested);}catch(const datapump::Error& e){error=e.what();}
        check(error.find("'default'")!=std::string::npos&&error.find("shared audio route")!=std::string::npos&&
              error.find(std::error_code(EBUSY,std::generic_category()).message())!=std::string::npos,
              "default failure omitted its busy reason or shared-audio guidance");
        check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.opens==0,
              "failed default capture opened an unrequested endpoint");
        rejects([&]{a::play(std::vector<float>(10,.1f),48000,requested);});
        check(f::state.attempts==std::vector<std::string>{"default","default"}&&f::state.opens==0,
              "failed default playback opened an unrequested endpoint");
    }
    f::reset();f::state.hints={{"default:CARD=Good",""},{"plughw:CARD=Good,DEV=0",""}};
    f::state.available={"default","default:CARD=Good","plughw:CARD=Good,DEV=0"};
    f::state.wrong_format={"default"};
    rejects([&]{a::play(std::vector<float>(100,.25f),96000,"default");});
    check(!f::state.attempts.empty()&&std::all_of(f::state.attempts.begin(),f::state.attempts.end(),
              [](const auto& id){return id=="default";})&&f::state.opens==f::state.closes,
          "default format negotiation changed endpoint or leaked a stream");
    for(const auto& device:std::vector<std::string>{"default:CARD=Generic_1","plughw:CARD=Generic_1,DEV=0","pulse","pipewire"}) {
        f::reset();f::state.available={device};
        const auto captured=a::record(.01,48000,device);
        a::play(std::vector<float>(10,.1f),48000,device);
        check(captured.size()==480&&f::state.selected==device&&f::state.opens==f::state.closes&&f::state.live==0,
              "explicit audio endpoint was unavailable or changed");
    }
    f::reset();f::state.available={"default"};bool simultaneous=false;
    a::capture(48000,"default",[&](std::span<const float>) {
        a::capture(48000,"default",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;});
        return false;
    });
    check(simultaneous&&f::state.live==0&&f::state.opens==2&&f::state.closes==2,
          "independent shared capture streams blocked or leaked each other");
}
void default_shared_routes() {
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2,.00004f};
    const auto setup=[](std::vector<std::string> available,std::vector<f::Hint> hints) {
        f::reset();f::state.available=std::move(available);f::state.hints=std::move(hints);
        f::state.supported_channels={1,2};
    };
    setup({"default","pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.hint_calls==0,
          "working default lost priority or unnecessarily enumerated routes");
    for(const auto& requested:std::vector<std::string>{"default",""}) {
        for(const auto& server:std::vector<std::string>{"pipewire","pulse"}) {
            setup({server},{{server,""}});
            const auto captured=a::record(.01,48000,requested);
            check(captured.size()==480&&f::state.selected==server&&f::state.hints_freed==1,
                  "default capture did not select advertised shared server");
            a::play(samples,48000,requested,{},{},a::ChannelMode::stereo,{});
            check(f::state.selected==server&&f::state.hints_freed==2&&f::state.live==0,
                  "default options playback did not select advertised shared server");
        }
    }
    setup({"pipewire","pulse"},{{"pulse",""},{"pipewire",""},{"pipewire",""}});
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default","pipewire"},
          "shared fallback did not prefer pipewire independently of hint order");
    setup({"pulse"},{{"pulse",""},{"pipewire",""},{"pipewire",""}});
    f::state.open_errors={{"pipewire",-EBUSY}};
    a::play(samples,48000);
    check(f::state.attempts==std::vector<std::string>{"default","pipewire","pulse"}&&f::state.hints_freed==1,
          "shared fallback repeated duplicate hints or stopped before pulse");
    for(const bool recording:{false,true}) {
        setup({"pipewire","pulse"},{{"pipewire",recording?"Output":"Input"},{"pulse",recording?"Input":"Output"}});
        if(recording)a::record(.01,48000);else a::play(samples,48000);
        check(f::state.attempts==std::vector<std::string>{"default","pulse"},
              "shared fallback ignored ALSA stream direction");
    }
    setup({"pipewire","pulse","hw:0","null","pipewire:CARD=USB"},
          {{"pulse","Duplex"},{"hw:0",""},{"null",""},{"pipewire:CARD=USB",""}});
    rejects([&]{a::play(samples,48000);});
    check(f::state.attempts==std::vector<std::string>{"default"}&&f::state.hints_freed==1,
          "shared fallback guessed an unadvertised or unsupported alias");
    for(const auto& device:std::vector<std::string>{"custom","default:CARD=USB","hw:0","pulse","pipewire"}) {
        setup({},{{"pipewire",""},{"pulse",""}});
        rejects([&]{a::play(samples,48000,device);});
        check(f::state.attempts==std::vector<std::string>{device}&&f::state.hint_calls==0,
              "explicit endpoint failure enumerated or opened fallback routes");
    }
    setup({"pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    rejects([&]{a::play(samples,48000,"default",{},{},a::ChannelMode::stereo,{1.,true});});
    check(f::state.attempts==std::vector<std::string>{"hw"}&&f::state.hint_calls==0,
          "exclusive default fell back to a shared route");
    setup({},{{"pipewire",""},{"pulse",""}});
    f::state.open_errors={{"default",-ENOENT},{"pipewire",-EBUSY},{"pulse",-EACCES}};
    std::string error;
    try{a::play(samples,48000);}catch(const datapump::Error& e){error=e.what();}
    for(const auto& endpoint:std::vector<std::string>{"default@48000Hz/2ch","pipewire@48000Hz/2ch","pulse@48000Hz/2ch"})
        check(error.find(endpoint)!=std::string::npos,"shared failure omitted an attempted endpoint");
    for(const auto code:{ENOENT,EBUSY,EACCES})
        check(error.find(std::error_code(code,std::generic_category()).message())!=std::string::npos,
              "shared failure omitted an endpoint's failure reason");
    check(f::state.hints_freed==1&&f::state.live==0,"all-failed shared negotiation leaked resources");
    setup({"pulse"},{{"pulse",""}});f::state.hint_error=-EIO;
    error.clear();
    try{a::play(samples,48000);}catch(const datapump::Error& e){error=e.what();}
    check(error.find("cannot enumerate shared audio routes")!=std::string::npos&&
          error.find(std::error_code(ENOENT,std::generic_category()).message())!=std::string::npos&&
          error.find(std::error_code(EIO,std::generic_category()).message())!=std::string::npos&&
          f::state.attempts==std::vector<std::string>{"default"}&&f::state.hints_freed==1,
          "failed hint enumeration hid the original failure or leaked hints");
    setup({"default","pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
    f::state.wrong_format={"default","pipewire"};f::state.supported_rates={48000};
    a::play(samples,96000);
    check(f::state.selected=="pulse"&&f::state.rate==48000&&f::state.live==0&&f::state.opens==f::state.closes,
          "format failure did not negotiate a shared server or leaked handles");
    check(std::find(f::state.attempts.begin(),f::state.attempts.end(),"pipewire")!=f::state.attempts.end(),
          "format fallback skipped the first advertised shared server");
    setup({"pulse"},{{"pulse",""}});bool simultaneous=false;
    a::capture(48000,"default",[&](std::span<const float>) {
        a::capture(48000,"default",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;});
        return false;
    });
    check(simultaneous&&f::state.live==0&&f::state.opens==2&&f::state.closes==2&&f::state.hints_freed==2,
          "shared fallback streams could not coexist or leaked resources");
    // Route recovery must not alter unity gain, resampling, or channel routing.
    for(const unsigned rate:{48000u,96000u})
        for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
            setup({"pulse"},{});f::state.supported_rates={48000};
            a::play(samples,rate,"pulse",{},{},channels);const auto explicit_pcm=f::state.played;
            setup({"pulse"},{{"pulse",""}});f::state.supported_rates={48000};
            a::play(samples,rate,"default",{},{},channels,{});
            check(f::state.played==explicit_pcm,"shared fallback changed unity PCM");
        }
    setup({"pulse"},{});const auto explicit_capture=a::record(.01,48000,"pulse");
    setup({"pulse"},{{"pulse",""}});
    check(a::record(.01,48000)==explicit_capture,"shared fallback changed capture PCM");
    for(const auto stage:{"before","open_failed","open_success","format_failed","format_success","hints","fallback_open"}) {
        setup({"pipewire","pulse"},{{"pipewire",""},{"pulse",""}});
        std::stop_source cancellation;
        const auto cancel=[&]{cancellation.request_stop();};
        const std::string when=stage;
        if(when=="before")cancel();
        else if(when=="open_failed")f::state.after_open=cancel;
        else if(when=="hints")f::state.after_hint=cancel;
        else if(when=="fallback_open")f::state.after_open=[&]{if(f::state.selected=="pipewire")cancel();};
        else {
            f::state.available.push_back("default");
            if(when=="open_success")f::state.after_open=cancel;
            else {f::state.after_configure=cancel;if(when=="format_failed")f::state.wrong_format={"default"};}
        }
        error.clear();
        try{a::play(samples,48000,"default",cancellation.get_token());}catch(const datapump::Error& e){error=e.what();}
        check(error=="audio operation cancelled"&&f::state.live==0&&f::state.opens==f::state.closes,
              "negotiation cancellation was ignored or leaked an open handle");
        check(f::state.hint_calls==f::state.hints_freed,"cancelled fallback enumeration leaked hints");
        const std::vector<std::string> expected=when=="before"?std::vector<std::string>{}:
            when=="fallback_open"?std::vector<std::string>{"default","pipewire"}:std::vector<std::string>{"default"};
        check(f::state.attempts==expected,"cancellation continued with format retries or another fallback");
    }
}
void channel_routing() {
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2};
    const std::vector<std::int16_t> pcm{0,8191,-16383,32767,-32767,32767,-32767};
    for(const auto& supported:std::vector<std::vector<unsigned>>{{1,2},{2}}) {
        f::reset();f::state.available={"stereo-card"};f::state.supported_channels=supported;
        a::play(samples,48000,"stereo-card");
        check(f::state.channels==2 && f::state.configured_channels==std::vector<unsigned>{2},"stereo playback must prefer two channels, including a stereo-only card");
        check(f::state.played.size()==pcm.size()*2,"default mono routing changed stereo frame count");
        for(std::size_t i=0;i<pcm.size();++i)
            check(f::state.played[2*i]==pcm[i] && f::state.played[2*i+1]==0,"default mono must preserve left and silence right PCM");
        check(f::state.live==0 && f::state.opens==f::state.closes,"stereo playback leaked its device");
        f::reset();f::state.available={"stereo-card"};f::state.supported_channels=supported;
        a::play(samples,48000,"stereo-card",{},{},false);
        check(f::state.channels==2 && f::state.played.size()==pcm.size()*2,"disabled mono changed stereo frame count");
        for(std::size_t i=0;i<pcm.size();++i)
            check(f::state.played[2*i]==pcm[i] && f::state.played[2*i+1]==pcm[i],"disabled mono must send the same PCM through both channels");
        check(f::state.live==0,"dual-channel playback leaked its device");
    }
    for(const auto mode:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
        const bool left=mode!=a::ChannelMode::right_mono,right=mode!=a::ChannelMode::left_mono;
        f::reset();f::state.available={"mono-card"};
        a::play(samples,48000,"mono-card",{},{},mode);
        check(f::state.channels==1 && f::state.configured_channels==std::vector<unsigned>{2,1},"mono-only card did not fall back at its original rate");
        check(f::state.played==pcm,"mono-only device lost or changed transmit PCM");
        check(f::state.live==0 && f::state.opens==f::state.closes,"mono fallback leaked a rejected stereo device");
        f::reset();f::state.available={"default"};f::state.supported_channels={2};f::state.write_limit=31;
        std::size_t generated=0;
        a::playback(48000,"default",[&](std::span<float> chunk) {
            check(chunk.size()<=2400,"stereo playback expanded the logical callback into channel samples");
            const auto count=std::min<std::size_t>(chunk.size(),10003-generated);
            for(std::size_t i=0;i<count;++i)chunk[i]=samples[(generated+i)%samples.size()];
            generated+=count;return count;
        },{},{},mode);
        check(f::state.played.size()==20006 && f::state.configured_streams==1,"streamed stereo changed duration or reopened a configured stream");
        check(std::all_of(f::state.write_frames.begin(),f::state.write_frames.end(),[](auto count){return count<=2400;}),"stereo driver writes count samples instead of frames");
        for(std::size_t i=0;i<10003;++i)
            check(f::state.played[2*i]==(left?pcm[i%pcm.size()]:0) && f::state.played[2*i+1]==(right?pcm[i%pcm.size()]:0),"partial stereo writes shifted channel or source frame alignment");
        check(f::state.live==0,"streamed stereo playback leaked its device");
        f::reset();f::state.available={"default"};f::state.supported_rates={44100};f::state.supported_channels={2};f::state.write_limit=31;
        std::vector<float> tone(9600+17);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/96000));
        a::play(tone,96000,"default",{},{},mode);
        const auto frames=(tone.size()*44100+95999)/96000;
        check(f::state.rate==44100 && f::state.channels==2 && f::state.played.size()==frames*2,"stereo rate fallback changed transmit duration");
        for(std::size_t i=0;i<frames;++i)
            check((left || f::state.played[2*i]==0) && (right || f::state.played[2*i+1]==0) &&
                  (!(left&&right) || f::state.played[2*i]==f::state.played[2*i+1]),"resampling changed channel routing");
        double error=0;
        for(std::size_t i=100;i+100<frames;++i)
            error=std::max(error,std::abs(f::state.played[2*i+(left?0:1)]/32767.-.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/44100)));
        check(error<.00015,"resampling and partial stereo writes changed active-channel phase/amplitude");
        check(f::state.live==0 && f::state.opens==f::state.closes,"stereo rate negotiation leaked a device");
    }
}
void audio_options() {
    check(a::exclusive_supported(),"ALSA lost explicit exclusive hardware support");
    const std::vector<float> samples{0,.25f,-.5f,1,-1,2,-2,.00004f,-.00004f};
    const std::vector<std::pair<double,std::vector<std::int16_t>>> vectors{
        {.0001,{0,0,-1,3,-3,6,-6,0,0}},
        {.001,{0,8,-16,32,-32,65,-65,0,0}},
        {.005,{0,40,-81,163,-163,327,-327,0,0}},
        {.5,{0,4095,-8191,16383,-16383,32767,-32767,0,0}},
        {1.,{0,8191,-16383,32767,-32767,32767,-32767,1,-1}},
        {1.05,{0,8601,-17202,32767,-32767,32767,-32767,1,-1}},
        {1.75,{0,14335,-28671,32767,-32767,32767,-32767,2,-2}}};
    for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
        for(const auto& [gain,pcm]:vectors) {
            f::reset();f::state.available={"default"};f::state.supported_channels={2};f::state.write_limit=2;
            a::play(samples,48000,"default",{},{},channels,{gain,false});
            check(f::state.played.size()==samples.size()*2,"transmit gain changed duration");
            for(std::size_t i=0;i<samples.size();++i) {
                const auto expected=pcm[i];
                check(f::state.played[2*i]==(channels==a::ChannelMode::right_mono?0:expected) &&
                      f::state.played[2*i+1]==(channels==a::ChannelMode::left_mono?0:expected),
                      "gain was applied before clipping/routing or changed unity PCM");
            }
        }
    }
    // Resampled 100% output must also retain the original conversion exactly.
    std::vector<float> tone(15001);
    for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(1.2*std::sin(i*.071));
    for(const unsigned rate:{48000u,96000u})
        for(const unsigned hardware_channels:{1u,2u})
            for(const auto channels:{a::ChannelMode::left_mono,a::ChannelMode::right_mono,a::ChannelMode::stereo}) {
                f::reset();f::state.available={"default"};f::state.supported_rates={48000};f::state.supported_channels={hardware_channels};
                a::play(tone,rate,"default",{},{},channels);const auto original=f::state.played;
                f::reset();f::state.available={"default"};f::state.supported_rates={48000};f::state.supported_channels={hardware_channels};
                a::play(tone,rate,"default",{},{},channels,{1.,false});
                check(f::state.played==original,"100% changed existing hardware PCM after resampling");
            }
    for(const double gain:{0.,-.01,.00001,1.75001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        f::reset();f::state.available={"default"};
        rejects([&]{a::play(samples,48000,"default",{},{},a::ChannelMode::left_mono,{gain,false});});
        check(f::state.attempts.empty(),"invalid gain opened hardware");
    }
    for(const auto& device:std::vector<std::string>{"hw:CARD=USB,DEV=2","plughw:CARD=USB,DEV=2","default:CARD=USB,DEV=2","sysdefault:CARD=USB,DEV=2","dmix:CARD=USB,DEV=2","dsnoop:CARD=USB,DEV=2"}) {
        const std::string shared_output="plug:SLAVE='dmix:CARD=USB,DEV=2'";
        const std::string shared_input="plug:SLAVE='dsnoop:CARD=USB,DEV=2'";
        const std::string hardware="hw:CARD=USB,DEV=2";
        f::reset();f::state.available={shared_input,shared_output,hardware};
        a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{});
        check(f::state.selected==shared_output,"shared output did not preserve selected hardware coordinates");
        const auto shared=a::record(.01,48000,device,1024*1024,{},{},{1.75,false});
        check(f::state.selected==shared_input,"shared input did not preserve selected hardware coordinates");
        f::state.captured=0;
        const auto exclusive=a::record(.01,48000,device,1024*1024,{},{},{.0001,true});
        check(f::state.selected==hardware && exclusive==shared,"exclusive capture changed device or transmit gain scaled capture");
        a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{1.,true});
        check(f::state.selected==hardware,"exclusive output did not use the selected hardware");
        check(f::state.live==0 && f::state.opens==f::state.closes,"sharing mode leaked a device");
    }
    f::reset();f::state.available={"hw"};
    a::play(samples,48000,"default",{},{},a::ChannelMode::left_mono,{1.,true});
    check(f::state.selected=="hw","exclusive default did not use ALSA's configured raw card");
    for(const auto& device:std::vector<std::string>{"pulse","pipewire","custom-route","front:CARD=USB,DEV=0","hw:CARD=USB,RATE=48000",
            "hw:CARD=USB,DEV=-1","hw:CARD=USB,DEV=-","hw:CARD=USB,CARD=Other","hw:CARD=USB,DEV=1,DEV=2","hw:CARD=USB,DEV=1,"}) {
        f::reset();rejects([&]{a::play(samples,48000,device,{},{},a::ChannelMode::left_mono,{1.,true});});
        check(f::state.attempts.empty(),"exclusive mode guessed hardware for an ambiguous route");
    }
    f::reset();f::state.available={"hw:CARD=USB,DEV=2"};
    rejects([&]{a::play(samples,48000,"hw:CARD=USB,DEV=2",{},{},a::ChannelMode::left_mono,{});});
    check(f::state.attempts==std::vector<std::string>{"plug:SLAVE='dmix:CARD=USB,DEV=2'"},"shared failure silently took exclusive hardware");
    f::reset();f::state.available={"plug:SLAVE='dsnoop:1,2'"};f::state.open_errors={{"hw:1,2",-EBUSY}};bool simultaneous=false;
    rejects([&]{a::capture(48000,"hw:1,2",[](std::span<const float>){return false;},{},{},{1.,true});});
    a::capture(48000,"plughw:1,2",[&](std::span<const float>) {
        a::capture(48000,"hw:1,2",[&](std::span<const float>) {simultaneous=f::state.live==2;return false;},{},{},{});
        return false;
    },{},{},{});
    check(simultaneous&&f::state.live==0,"explicit shared capture streams blocked one another");
    check(f::state.attempts==std::vector<std::string>{"hw:1,2","plug:SLAVE='dsnoop:1,2'","plug:SLAVE='dsnoop:1,2'"},
          "shared capture did not keep its own route after exclusive access was busy");
}
int main(){try{
    plugin_directories();selected_endpoint();default_shared_routes();
    channel_routing();audio_options();
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
    check(f::state.configured_streams==1 && callbacks>=5 && f::state.played.size()==10000,"streaming playback reopened or lost samples");
    for(std::size_t i=0;i<f::state.played.size();++i)check(f::state.played[i]==static_cast<std::int16_t>((static_cast<float>(i%1000)/1000)*32767),"partial writes reordered or regenerated source samples");
    check(f::state.live==0,"streaming playback leaked");
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){return chunk.size()+1;});});
    f::reset();f::state.available={"default"};rejects([&]{a::playback(48000,"default",[](std::span<float> chunk){chunk[0]=std::numeric_limits<float>::quiet_NaN();return 1;});});
    check(f::state.played.empty(),"invalid generated samples were played");
    // Rate selection belongs to the sound-card boundary, not the DSP clock.
    for(const unsigned hardware:{44100u,48000u,96000u}) {
        constexpr unsigned logical=96000;
        f::reset();f::state.available={"explicit-card"};f::state.supported_rates={hardware};
        a::StreamFormat observed;
        std::vector<float> tone(logical/5+17);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/logical));
        a::play(tone,logical,"explicit-card",{},[&](const auto& info){observed=info;});
        check(observed.logical_rate==logical && observed.hardware_rate==hardware,"negotiated clock metadata missing");
        check(observed.usable_passband_hz<=hardware*.5,"reported recoverable spectrum above hardware Nyquist");
        check(observed.workspace_bytes>0 && observed.workspace_bytes<512*1024,"converter workspace metadata invalid");
        check(f::state.played.size()==(tone.size()*hardware+logical-1)/logical,"playback resampling changed duration/EOF");
        check(std::all_of(f::state.attempts.begin(),f::state.attempts.end(),[](const auto& id){return id=="explicit-card";}),"rate negotiation changed an explicit device");
        double error=0;
        for(std::size_t i=100;i+100<f::state.played.size();++i)
            error=std::max(error,std::abs(f::state.played[i]/32767.-.5*std::sin(2*std::numbers::pi*1500*static_cast<double>(i)/hardware)));
        check(error<.00015,"negotiated playback altered tone phase/amplitude");
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.sample=[](std::size_t index,unsigned clock){return static_cast<std::int16_t>(16000*std::sin(2*std::numbers::pi*1700*static_cast<double>(index)/clock));};
        const auto receive=a::record(.2,logical,"default",1024*1024,{},[&](const auto& info){observed=info;});
        check(receive.size()==19200 && observed.hardware_rate==hardware,"capture clock conversion changed logical duration");
        error=0;
        for(std::size_t i=200;i<receive.size();++i)
            error=std::max(error,std::abs(receive[i]-(16000./32768)*std::sin(2*std::numbers::pi*1700*static_cast<double>(i)/logical)));
        check(error<.00015,"capture conversion altered logical sample timestamps");
        check(f::state.live==0,"resampled capture leaked stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={48000,192000};
    a::play(std::vector<float>(100),96000,"default");
    check(f::state.rate==192000,"negotiation reduced passband despite an available higher hardware clock");
    for(const unsigned logical:{64u,4800u}) {
        constexpr unsigned hardware=48000;
        const double frequency=logical/8.;
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.write_limit=31; // The driver may make much less progress on battery.
        a::StreamFormat observed;
        std::vector<float> tone(logical*4+1);
        for(std::size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.5*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/logical));
        a::play(tone,logical,"default",{},[&](const auto& info){observed=info;});
        check(observed.logical_rate==logical && observed.hardware_rate==hardware,"low DSP clock changed hardware identity/rate");
        check(std::all_of(f::state.configured.begin(),f::state.configured.end(),[](const auto& item){return item.second>=8000 && item.second<=384000;}),"audio hardware was asked to run at a sub-audio DSP clock");
        check(f::state.played.size()==(tone.size()*hardware+logical-1)/logical,"partial low-rate playback changed duration");
        double error=0;
        for(std::size_t i=hardware;i+hardware<f::state.played.size();++i)
            error=std::max(error,std::abs(f::state.played[i]/32767.-.5*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/hardware)));
        check(error<.00015,"partial driver writes distort low-rate playback");
        f::reset();f::state.available={"default"};f::state.supported_rates={hardware};
        f::state.read_limit=73;
        f::state.sample=[&](std::size_t index,unsigned clock){return static_cast<std::int16_t>(16000*std::sin(2*std::numbers::pi*frequency*static_cast<double>(index)/clock));};
        const auto received=a::record(4,logical,"default",1024*1024,{},[&](const auto& info){observed=info;});
        check(received.size()==logical*4 && observed.workspace_bytes<5*1024*1024,"low-rate capture lost duration or exceeded bounded DSP memory");
        error=0;
        for(std::size_t i=logical;i<received.size();++i)
            error=std::max(error,std::abs(received[i]-(16000./32768)*std::sin(2*std::numbers::pi*frequency*static_cast<double>(i)/logical)));
        check(error<.00015,"multistage capture distorted phase or amplitude");
        f::state.captured=0;f::state.read_limit=173;
        const auto other_chunks=a::record(4,logical,"default",1024*1024);
        check(received==other_chunks,"varying driver capture chunk sizes changes modem PCM");
        check(f::state.live==0,"low-rate conversion leaked an audio stream");
    }
    f::reset();f::state.available={"default"};f::state.supported_rates={44100};
    std::stop_source cancelled;
    rejects([&]{a::playback(96000,"default",[&](std::span<float> values){cancelled.request_stop();std::fill(values.begin(),values.end(),.1f);return values.size();},cancelled.get_token());});
    check(f::state.played.empty(),"resampling played PCM after producer cancellation");
    f::reset();f::state.available={"default"};f::state.supported_rates={44100};
    std::stop_source capture_cancel;
    rejects([&]{a::capture(96000,"default",[&](std::span<const float> values){check(values.size()<=4096,"capture resampling exceeded bounded callback size");capture_cancel.request_stop();return false;},capture_cancel.get_token());});
    std::cout<<"ALSA default resolution and streaming tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
