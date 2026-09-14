#include "../src/gui/state.hpp"
#include <iostream>
#include <limits>

using namespace datapump;
void check(bool value) { if (!value) throw Error("Native GUI policy test failed"); }
DecodedPacket packet(std::uint8_t id, std::size_t size) {
    DecodedPacket result;
    result.message.id[0]=id;
    result.message.data.resize(size,id);
    return result;
}
void retained_raw_bits() {
    gui::Signals signals;
    gui::SignalLine decoded;
    decoded.text="t";decoded.raw_bits="010";decoded.received_bits=3;
    decoded.complete=true;decoded.pattern_score=25.;
    signals.update(decoded);
    check(signals.copy_raw_bits(0)=="010" && signals.copy_text(0)=="t" &&
          signals.copy_bytes(0)==Bytes{'t'} && !signals.copy_bits(0) && !signals.copy_id(0));
    check(signals.lines()[0].raw_bits=="010" && !signals.copy_raw_bits(1));

    const auto unavailable=[&](gui::SignalLine line) {
        signals.clear();signals.update(std::move(line));check(!signals.copy_raw_bits(0));
    };
    auto pending=decoded;pending.complete=false;unavailable(pending);
    auto partial=decoded;partial.raw_bits="01";unavailable(partial);
    check(signals.lines()[0].raw_bits.empty());
    partial=decoded;partial.expected_bits=4;unavailable(partial);
    for (const auto& invalid : {std::string{},std::string("01x"),std::string("0 1"),std::string("01\0",3)}) {
        auto malformed=decoded;malformed.raw_bits=invalid;unavailable(malformed);
        check(signals.lines()[0].raw_bits.empty());
    }
    for (const auto score : {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid_score=decoded;invalid_score.pattern_score=score;unavailable(invalid_score);
    }
    auto missing_score=decoded;missing_score.pattern_score.reset();unavailable(missing_score);
    auto verified=decoded;verified.validated=true;unavailable(verified);
    auto file=decoded;file.text_message=false;unavailable(file);
    auto empty=decoded;empty.text.clear();unavailable(empty);

    auto bounded=decoded;bounded.raw_bits=std::string(4096,'0');bounded.received_bits=4096;
    signals.clear();signals.update(bounded);
    check(signals.copy_raw_bits(0)==bounded.raw_bits);
    bounded.raw_bits.push_back('0');++bounded.received_bits;unavailable(bounded);
    check(signals.lines()[0].raw_bits.empty());

    gui::SignalLine binary;
    binary.binary=true;binary.text="01110100";binary.received_bits=8;binary.complete=true;
    signals.clear();signals.update(binary);
    check(signals.copy_raw_bits(0)=="01110100" && signals.copy_text(0)=="t" && !signals.copy_bits(0));
    binary.text="010";binary.received_bits=3;signals.update(binary);
    check(signals.copy_raw_bits(0)=="010" && signals.copy_bits(0)=="010" && !signals.copy_text(0));
    binary.complete=false;unavailable(binary);
    binary.complete=true;binary.expected_bits=4;unavailable(binary);
    binary.expected_bits=0;binary.text="01";unavailable(binary);
    binary.text="01x";unavailable(binary);
    binary.text=std::string(4097,'0');binary.received_bits=4097;unavailable(binary);
    check(signals.lines()[0].text.size()==4096);
}
int main() {
    try {
        retained_raw_bits();
        check(gui::parse_binary_bits("0")==Bytes({0}));
        check(gui::parse_binary_bits("000101")==Bytes({0,0,0,1,0,1}));
        check(gui::parse_binary_bits("\t00 01\r\n0\f1\v")==Bytes({0,0,0,1,0,1}));
        check(gui::parse_binary_bits(std::string(1025,'0'))==Bytes(1025,0));
        for (const std::string& invalid : {std::string{},std::string(" \t\r\n"),std::string("0102"),
                                         std::string("0b101"),std::string("0,1"),std::string("01x"),
                                         std::string("0\0" "1",3),std::string("0\xc2\xa0" "1")}) {
            bool rejected_bits=false;
            try { (void)gui::parse_binary_bits(invalid); } catch (const Error&) { rejected_bits=true; }
            check(rejected_bits);
        }
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
        check(gui::signal_preamble_label(signals.lines()[0])=="Preamble --");
        check(gui::signal_data_label(signals.lines()[0])=="Data pre-FEC pending");
        signals.update({7,1500,"corrected text",false,{}});
        check(signals.lines().size()==1 && signals.lines()[0].text=="corrected text");
        signals.update({7,1500,"corrected text",true,"verified-id"});
        check(signals.copy_id(0)=="verified-id");
        gui::SignalLine quality{7,1500,"corrected text",true,"verified-id",true,75.0,
                                PacketBitAccuracy{800,3}};
        check(gui::signal_preamble_label(quality)=="Preamble 75.0%");
        quality.preamble_received_percent=99.96;
        check(gui::signal_preamble_label(quality)=="Preamble >99.9%");
        quality.preamble_received_percent=100.;
        check(gui::signal_preamble_label(quality)=="Preamble 100.0%");
        quality.preamble_received_percent=75.;
        check(gui::signal_data_label(quality)=="Data 99.62% pre-FEC");
        signals.update(quality);
        signals.update({7,1500,"late unvalidated text",false,{}});
        check(signals.lines()[0].text=="corrected text" && signals.copy_id(0)=="verified-id");
        check(signals.lines()[0].preamble_received_percent==75.0 &&
              signals.lines()[0].pre_fec_accuracy->corrected_data_bits==3);
        quality.pre_fec_accuracy=PacketBitAccuracy{800,0};
        check(gui::signal_data_label(quality)=="Data 100% pre-FEC");
        quality.pre_fec_accuracy=PacketBitAccuracy{8000000,1};
        check(gui::signal_data_label(quality)=="Data >99.99% pre-FEC");
        quality.pre_fec_accuracy=PacketBitAccuracy{800,800};
        check(gui::signal_data_label(quality)=="Data 0.00% pre-FEC");
        for (const auto invalid : {PacketBitAccuracy{0,0},PacketBitAccuracy{10,11}}) {
            quality.pre_fec_accuracy=invalid;
            check(gui::signal_data_label(quality)=="Data pre-FEC --");
        }
        quality.pre_fec_accuracy.reset();
        check(gui::signal_data_label(quality)=="Data pre-FEC --");
        quality.validated=false; quality.pre_fec_accuracy=PacketBitAccuracy{800,0};
        check(gui::signal_data_label(quality)=="Data pre-FEC pending");
        for (const auto invalid : {-1.,101.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            quality.preamble_received_percent=invalid;
            check(gui::signal_preamble_label(quality)=="Preamble --");
        }
        for (std::uint64_t id=8;id<80;++id) signals.update({id,1500,std::string(5000,'a'),false,{}});
        check(signals.lines().size()==64 && signals.lines().back().text.size()==4096);
        signals.clear(); check(signals.lines().empty());
        signals.update({90,1500,"readme.txt",true,"file-id",false});
        check(!signals.copy_id(0)); // Even UTF-8 files require explicit Save.
        gui::SignalLine binary_line{91,1500,"Receiving binary...",false,{},false};
        binary_line.binary=true;
        binary_line.expected_bits=3;
        check(gui::signal_status_label(binary_line)=="binary pending");
        check(gui::signal_preamble_label(binary_line)=="Preamble none");
        check(gui::signal_data_label(binary_line)=="FEC off");
        signals.clear(); signals.update(binary_line);
        check(!signals.copy_id(0) && !signals.copy_bits(0));
        binary_line.text="00"; binary_line.received_bits=2;
        signals.update(binary_line);
        check(signals.lines()[0].text=="00" && !signals.copy_bits(0));
        binary_line.text="001"; binary_line.complete=true; binary_line.received_bits=3;
        signals.update(binary_line);
        check(gui::signal_status_label(signals.lines()[0])=="binary received");
        check(!gui::signal_byte_aligned(signals.lines()[0]) && gui::signal_display_text(signals.lines()[0])=="001");
        check(!signals.copy_id(0) && !signals.lines()[0].validated && signals.lines()[0].complete);
        check(signals.copy_bits(0)=="001" && !signals.copy_text(0) && !signals.copy_bytes(0));
        binary_line.text="0"; binary_line.complete=false;
        signals.update(binary_line);
        check(signals.lines()[0].text=="001" && signals.lines()[0].complete);
        check(signals.copy_bits(0)=="001");
        // A raw result never gains packet authentication or accuracy labels,
        // even if a caller accidentally supplies unrelated packet fields.
        binary_line.id=92; binary_line.text="001"; binary_line.complete=true;
        binary_line.validated=true; binary_line.packet_id="not-a-raw-packet-id";
        binary_line.preamble_received_percent=100.; binary_line.pre_fec_accuracy=PacketBitAccuracy{8,0};
        signals.update(binary_line);
        check(!signals.lines()[1].validated && signals.lines()[1].packet_id.empty() &&
              !signals.lines()[1].preamble_received_percent && !signals.lines()[1].pre_fec_accuracy);
        check(!signals.copy_id(1) && signals.copy_bits(1)=="001");
        for (const auto& invalid_text : {std::string{},std::string("00x"),std::string("0 1"),std::string("00\0",3)}) {
            binary_line.id=93; binary_line.text=invalid_text;
            signals.update(binary_line);
            check(!signals.copy_bits(2));
        }
        binary_line.text="001"; binary_line.expected_bits=4;
        signals.update(binary_line); check(!signals.copy_bits(2));
        binary_line.expected_bits=0; binary_line.received_bits=0;
        signals.update(binary_line); check(!signals.copy_bits(2));
        binary_line.received_bits=3;binary_line.pattern_score=25.;
        signals.update(binary_line);check(signals.copy_bits(2)=="001");
        binary_line.pattern_score.reset();
        binary_line.text=std::string(5000,'0'); binary_line.expected_bits=binary_line.received_bits=5000;
        signals.update(binary_line);
        check(signals.lines()[2].complete && signals.lines()[2].text.size()==4096 && !signals.copy_bits(2));
        check(gui::signal_data_label(signals.lines()[2])=="No checksum / FEC / prefix");
        check(gui::signal_byte_aligned(signals.lines()[2]) && gui::signal_status_label(signals.lines()[2])=="text received");
        std::string escaped_prefix;
        for(std::size_t index=0;index<512;++index)escaped_prefix+="\\x00";
        check(gui::signal_display_text(signals.lines()[2])==escaped_prefix);
        check(!signals.copy_text(2) && !signals.copy_bytes(2));
        check(!signals.copy_bits(3) && !signals.copy_text(3) && !signals.copy_bytes(3));

        gui::Signals byte_signals;
        gui::SignalLine byte_line;
        byte_line.id=100; byte_line.binary=true; byte_line.text_message=false;
        byte_line.text="01001000"; byte_line.received_bits=8; byte_line.expected_bits=32;
        byte_signals.update(byte_line);
        check(!gui::signal_byte_aligned(byte_signals.lines()[0]));
        check(gui::signal_status_label(byte_signals.lines()[0])=="binary pending" &&
              gui::signal_display_text(byte_signals.lines()[0])=="01001000");
        check(!byte_signals.copy_bits(0) && !byte_signals.copy_text(0) && !byte_signals.copy_bytes(0));
        byte_line.complete=true; byte_line.text="01001000011001010110110001110000"; byte_line.received_bits=32;
        byte_signals.update(byte_line);
        check(byte_signals.lines().size()==1 && byte_signals.lines()[0].binary && byte_signals.lines()[0].text==byte_line.text);
        check(gui::signal_byte_aligned(byte_signals.lines()[0]) && gui::signal_status_label(byte_signals.lines()[0])=="text received");
        check(gui::signal_display_text(byte_signals.lines()[0])=="Help" && byte_signals.copy_text(0)=="Help");
        check(byte_signals.copy_bytes(0)==Bytes({'H','e','l','p'}) && !byte_signals.copy_bits(0) && !byte_signals.copy_id(0));

        byte_line.id=101; byte_line.text="1100001110101001"; byte_line.received_bits=16; byte_line.expected_bits=0;
        byte_line.pattern_score=25.;
        byte_signals.update(byte_line);
        check(gui::signal_display_text(byte_signals.lines()[1])=="\xc3\xa9" && byte_signals.copy_text(1)=="\xc3\xa9");
        check(byte_signals.copy_bytes(1)==Bytes({0xc3,0xa9}) && !byte_signals.copy_bits(1));

        byte_line.id=102; byte_line.text="000000001111111101011100"; byte_line.received_bits=byte_line.expected_bits=24;
        byte_signals.update(byte_line);
        check(gui::signal_display_text(byte_signals.lines()[2])=="\\x00\\xFF\\\\" && byte_signals.copy_text(2)=="\\x00\\xFF\\\\");
        check(byte_signals.copy_bytes(2)==Bytes({0,0xff,'\\'}) && !byte_signals.copy_bits(2));

        byte_line.id=103; byte_line.text="01001000"; byte_line.received_bits=8; byte_line.expected_bits=16;
        byte_signals.update(byte_line);
        check(!byte_signals.copy_bits(3) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        byte_line.expected_bits=8; byte_line.text="0100100";
        byte_signals.update(byte_line);
        check(!byte_signals.copy_bits(3) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        byte_line.text="0100100x";
        byte_signals.update(byte_line);
        check(!byte_signals.copy_bits(3) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        byte_line.text.clear(); byte_line.received_bits=byte_line.expected_bits=0;
        byte_signals.update(byte_line);
        check(!gui::signal_byte_aligned(byte_signals.lines()[3]) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
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
        check(gui::format_bit_rate(1200)=="1.2 kbit/s");
        check(gui::format_bit_rate(.025)=="0.025 bit/s");
        check(gui::format_bit_rate(6.34e-9)=="6.34e-09 bit/s");
        check(gui::format_bit_rate(std::numeric_limits<double>::denorm_min()).find("e-")!=std::string::npos);
        check(gui::format_bit_rate(0)=="0 bit/s");
        check(gui::format_bit_rate(std::numeric_limits<double>::quiet_NaN())=="Unavailable");
        check(gui::key_entry_names(" Home, Portable , Caf\xc3\xa9 ")==std::vector<std::string>({"Home","Portable","Caf\xc3\xa9"}));
        const std::vector<std::string> choice_names{"A|B","None","_Home","Home","A&B","AB","Path/Back\\slash"};
        check(gui::key_choice_labels(choice_names)==std::vector<std::string>({
            "None","1. A|B","2. None","3. _Home","4. Home","5. A&B","6. AB","7. Path/Back\\slash"}));
        check(gui::key_choice_labels({})==std::vector<std::string>({"None"}));
        for (const auto* invalid : {"", " ", ",Home", "Home,", "Home,,Portable"}) {
            bool invalid_names=false;
            try { (void)gui::key_entry_names(invalid); } catch (const Error&) { invalid_names=true; }
            check(invalid_names);
        }
        bool empty_folder=false;
        try { (void)gui::folder_uri({}); } catch (const Error&) { empty_folder=true; }
        check(empty_folder);
#ifdef _WIN32
        check(gui::folder_uri(std::filesystem::path(u8"C:/Key folders/Caf\u00e9 #?%$`'\""))==
              "file:///C:/Key%20folders/Caf%C3%A9%20%23%3F%25%24%60%27%22");
        check(gui::folder_uri(std::filesystem::path(u8"//server/share/Key folders"))=="file://server/share/Key%20folders");
#else
        check(gui::folder_uri(std::filesystem::path(u8"/tmp/Key folders/Caf\u00e9 #?%$`'\""))==
              "file:///tmp/Key%20folders/Caf%C3%A9%20%23%3F%25%24%60%27%22");
#endif
        check(gui::folder_uri(std::filesystem::current_path()/"folder"/".."/"keys")==
              gui::folder_uri(std::filesystem::current_path()/"keys"));
        gui::PlotReplayPolicy plots;
        auto change=plots.observe(1,0,false);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(1,0,false);
        check(!change.update_plots && !change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(2,1,true,0);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        change=plots.observe(3,1,true,0); // Repeated delivery is not another frame.
        check(!change.update_plots && !change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(4,1,true,1);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        // A scheduling gap may skip frames; the newest delivery is still
        // shown once, even if its unrelated source sequence is unchanged.
        change=plots.observe(4,1,true,7);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(4,1,true,7);
        check(!change.update_plots && !change.append_waterfall && !change.clear_waterfall);
        // Cancellation/completion returns every plot to live even when the
        // source sequence has not changed since the final replay frame.
        change=plots.observe(4,1,false);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(5,1,false);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(5,2,true,0);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        // A new transmission can replace a replay without a live poll between.
        change=plots.observe(6,3,true,0);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        plots.reset();
        change=plots.observe(6,3,true,2);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        std::cout<<"Native GUI policy tests passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
