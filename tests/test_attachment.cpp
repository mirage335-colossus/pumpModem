#include "datapump/attachment.hpp"
#include "datapump/transfer.hpp"
#include <iostream>
#include <limits>
using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
void convention() {
    Message message;message.kind=MessageKind::file;message.filename="notes caf\xc3\xa9.bin";
    message.data={0,255,'x',0};message.repeatable=true;
    const auto source=attachment::encode(message,4);
    const auto label=attachment::marker(message.filename);
    check(label=="#ATTACHMENT### notes caf\xc3\xa9.bin ###ATTACHMENT# ","attachment uses the exact visible source marker");
    check(Bytes(source.begin()+label.size(),source.end())==message.data,"marker changed attachment bytes");
    Message received;received.data=source;attachment::interpret(received,4);
    check(received.kind==MessageKind::file && received.filename==message.filename && received.data==message.data && !received.repeatable,
          "attachment marker must restore filename/exact bytes and disable repeatable");
    for(const auto data:{Bytes{'h','i'},Bytes{0,255,0},Bytes{}}) {
        Message plain;plain.data=data;plain.filename="received.bin";
        attachment::interpret(plain,16);
        check(plain.kind==MessageKind::text && plain.filename.empty() && plain.data==data,"ordinary binary/text bytes must not become files");
    }
    for(const auto& name:{std::string{},std::string("../outside.bin"),std::string("a\\b"),std::string("a\nb"),
        std::string("a\0b",3),std::string(256,'x'),std::string("\xc0\x80",2),std::string(".."),std::string("a ###ATTACHMENT# b"),
        std::string("a\xc2\x80" "b"),std::string("a\xc2\x85" "b"),std::string("a\xc2\x9f" "b")}) {
        bool rejected=false;try{attachment::marker(name);}catch(const Error&){rejected=true;}
        check(rejected,"invalid local attachment name was accepted");
        const auto raw=std::string(attachment::prefix)+name+std::string(attachment::suffix)+"payload";
        Message plain;plain.data=Bytes(raw.begin(),raw.end());const auto before=plain.data;
        attachment::interpret(plain,1024);
        // A delimiter embedded in a name is indistinguishable from the user
        // deliberately ending its visible marker there; TX rejects that name.
        if(name.find(attachment::suffix)==name.npos)
            check(plain.kind==MessageKind::text && plain.data==before,"malformed marker must remain ordinary bytes");
    }
    Message long_name;long_name.kind=MessageKind::file;long_name.filename=std::string(255,'x');
    auto raw=attachment::encode(long_name,0);Message empty;empty.data=raw;attachment::interpret(empty,0);
    check(empty.kind==MessageKind::file && empty.data.empty(),"empty attachment and maximum bounded filename must survive");
    const std::string incomplete="#ATTACHMENT### never closes";
    Message plain;plain.data.assign(incomplete.begin(),incomplete.end());attachment::interpret(plain,100);
    check(plain.kind==MessageKind::text && plain.data.size()==incomplete.size(),"truncated application marker is not an attachment");
}
void completed_source_only() {
    for(const bool compressed:{false,true}) {
        transfer::Options value;value.timestamp=1800000000;value.search_seconds=0;value.compression=compressed;value.content_limit=4;
        Message sent;sent.kind=MessageKind::file;sent.filename="zero.bin";sent.data={0,1,2,0};sent.repeatable=true;
        const auto wire=transfer::message_wire_bits(sent,value);
        check(!transfer::estimate(sent,value).repeatable_allowed,"attachments must not offer repeatable transmission");
        transfer::StreamReceiver receiver(value,value.timestamp);modem::PatternBurst burst;
        burst.bits=wire;burst.score=100;burst.stream_first_sample=100;
        const auto pending=receiver.push(burst);
        check(!pending.content_validated && pending.content.message.filename.empty(),"attachment interpretation ran before physical end");
        burst.bits.clear();burst.first_stream_symbol=wire.size();burst.complete=true;
        const auto received=receiver.push(burst);
        check(received.content_validated && received.content.message.kind==MessageKind::file && received.content.message.filename==sent.filename &&
              received.content.message.data==sent.data,"attachment source roundtrip exceeded content quota or lost its marker");
    }
}
void rejected_source_is_not_published() {
    for(const bool compressed:{false,true})for(const bool file:{false,true}) {
        transfer::Options sender;sender.timestamp=1800000000;sender.search_seconds=0;
        sender.compression=compressed;sender.content_limit=3;
        Message sent;sent.data={0,255,0};
        if(file){sent.kind=MessageKind::file;sent.filename="oversized.bin";}
        const auto wire=transfer::message_wire_bits(sent,sender);
        auto value=sender;value.content_limit=2;
        transfer::StreamReceiver receiver(value,value.timestamp);modem::PatternBurst burst;
        burst.bits=wire;burst.score=100;burst.stream_first_sample=100;burst.complete=true;
        const auto received=receiver.push(burst);
        check(received.stream_complete && !received.content_validated && !received.error.empty() &&
              received.content.message.data.empty() && received.content.message.filename.empty() &&
              received.content.message.kind==MessageKind::text,
              "a source rejected by the application quota must publish neither decoded bytes nor attachment metadata");
    }
    bool rejected=false;
    try{(void)attachment::source_limit(std::numeric_limits<std::size_t>::max());}catch(const Error&){rejected=true;}
    check(rejected,"attachment quota padding must reject address-space overflow");
}
}
int main(){try{convention();completed_source_only();rejected_source_is_not_published();std::cout<<"Attachment application convention passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
