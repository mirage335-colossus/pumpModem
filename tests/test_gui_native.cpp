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
        std::cout<<"Native GUI policy tests passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
