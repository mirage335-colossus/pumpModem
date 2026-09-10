#include "../src/gui/state.hpp"
#include <iostream>

using namespace datapump;
void check(bool value) { if (!value) throw Error("Native GUI policy test failed"); }
DecodedPacket packet(std::uint8_t id, std::size_t size) {
    DecodedPacket result;
    result.message.id[0]=id;
    result.message.data.resize(size,id);
    return result;
}
int main() {
    try {
        gui::Inbox inbox(5);
        inbox.put(packet(1,3));
        inbox.put(packet(1,2));
        check(inbox.items().size()==1 && inbox.size_bytes()==2);
        inbox.put(packet(2,4));
        check(inbox.items().size()==1 && inbox.items()[0].message.id[0]==2);
        bool rejected=false;
        try { inbox.put(packet(3,6)); } catch (const Error&) { rejected=true; }
        check(rejected && inbox.size_bytes()==4);
        inbox.clear();
        check(inbox.items().empty() && inbox.size_bytes()==0);
        gui::Inbox empty_payloads(1);
        for (std::size_t n=0;n<4097;++n) {
            auto item=packet(static_cast<std::uint8_t>(n),0);
            item.message.id[1]=static_cast<std::uint8_t>(n>>8);
            empty_payloads.put(std::move(item));
        }
        check(empty_payloads.items().size()==4096 && empty_payloads.size_bytes()==0);
        check(gui::valid_clipboard_text(Bytes{'h','i','\n',0xc3,0xa9,0xf0,0x9f,0x8c,0x8d}));
        for (const auto& bad: {Bytes{0},Bytes{0xc0,0x80},Bytes{0xed,0xa0,0x80},
                              Bytes{0xf4,0x90,0x80,0x80},Bytes{0xe2,0x82},Bytes{0x80}})
            check(!gui::valid_clipboard_text(bad));
        check(gui::valid_clipboard_text({}));
        check(gui::display_label("name\n\tlabel")=="name  label");
        check(gui::id_label(packet(0xab,0).message).substr(0,2)=="ab");
        gui::Signals signals;
        signals.update({7,1500,"uncertain tezt",false,{}});
        check(!signals.copy_id(0));
        signals.update({7,1500,"corrected text",false,{}});
        check(signals.lines().size()==1 && signals.lines()[0].text=="corrected text");
        signals.update({7,1500,"corrected text",true,"verified-id"});
        check(signals.copy_id(0)=="verified-id");
        signals.update({7,1500,"late unvalidated text",false,{}});
        check(signals.lines()[0].text=="corrected text" && signals.copy_id(0)=="verified-id");
        for (std::uint64_t id=8;id<80;++id) signals.update({id,1500,std::string(5000,'a'),false,{}});
        check(signals.lines().size()==64 && signals.lines().back().text.size()==4096);
        signals.clear(); check(signals.lines().empty());
        signals.update({90,1500,"readme.txt",true,"file-id",false});
        check(!signals.copy_id(0)); // Even UTF-8 files require explicit Save.
        gui::Inbox mixed;
        mixed.put(packet(1,3)); // Text belongs to the signal browser only.
        auto file=packet(2,4); file.message.kind=MessageKind::file; file.message.filename="payload.bin";
        mixed.put(file);
        mixed.put(packet(3,2));
        auto screenshot=packet(4,5); screenshot.message.kind=MessageKind::screenshot; screenshot.message.filename="capture.png";
        mixed.put(screenshot);
        const auto files=mixed.file_items();
        check(files.size()==2 && files[0]->message.id[0]==2 && files[1]->message.id[0]==4);
        check(mixed.items().size()==4); // Verified text remains available for exact clipboard copy.
        gui::TransmissionPolicy policy;
        const auto time=gui::TransmissionPolicy::Clock::time_point{};
        policy.started(false,false,time);
        bool concurrent_rejected=false;
        try { policy.started(true,false,time); } catch (const Error&) { concurrent_rejected=true; }
        check(concurrent_rejected); // All modes allow only one active transmission.
        policy.finished(time+std::chrono::seconds(1));
        check(policy.remaining(false,false,time+std::chrono::seconds(1)).count()==0);
        policy.started(true,true,time+std::chrono::seconds(1));
        policy.finished(time+std::chrono::seconds(2));
        check(policy.remaining(true,true,time+std::chrono::seconds(2)).count()==0);
        check(policy.remaining(false,true,time+std::chrono::seconds(2)).count()==0);
        policy.started(false,true,time+std::chrono::seconds(2));
        policy.finished(time+std::chrono::seconds(3));
        check(policy.remaining(false,true,time+std::chrono::seconds(4))==std::chrono::seconds(5));
        check(policy.remaining(true,true,time+std::chrono::seconds(4)).count()==0);
        check(policy.remaining(false,false,time+std::chrono::seconds(4)).count()==0);
        policy.started(true,false,time+std::chrono::seconds(4));
        policy.finished(time+std::chrono::seconds(5));
        check(policy.remaining(false,true,time+std::chrono::seconds(5))==std::chrono::seconds(4));
        policy.started(false,true,time+std::chrono::seconds(9));
        policy.abort_start();
        check(policy.remaining(false,true,time+std::chrono::seconds(9)).count()==0);
        std::cout<<"Native GUI policy tests passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
