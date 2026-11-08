#include "datapump/live.hpp"
#include "datapump/received_text.hpp"
#include <iostream>
#include <string_view>
int main() {
    using namespace datapump;
    try {
        live::Session session;live::Settings settings;session.start(settings);
        const auto snapshot=session.snapshot();
        if(snapshot.running||snapshot.transmitting||!snapshot.received.empty()||
           snapshot.status.find("unavailable")==std::string::npos)throw Error("retired Robust backend appears active");
        const auto refuses=[&](auto request) {
            try {request();}catch(const Error& error) {
                if(std::string_view(error.what()).find("canonical triad")!=std::string_view::npos)return;
                throw;
            }
            throw Error("retired Robust operation did not refuse");
        };
        refuses([&]{session.transmit(Message{});});
        refuses([&]{session.transmit_bits(Bytes{0,0,1});});
        refuses([&]{session.transmit_noise();});
        refuses([&]{session.transmit_helper();});
        if(received_text(std::string("hello\x1b<>\n",9))!="hello___\n")throw Error("received text restriction changed");
        if(!session.try_suspend_capture())throw Error("unavailable backend claimed an audio device");
        session.stop();
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
