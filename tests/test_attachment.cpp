#include "datapump/attachment.hpp"
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
        std::string("a\xc2\x80" "b"),std::string("a\xc2\x85" "b"),std::string("a\xc2\x9f" "b"),
        std::string("\xc2"),std::string("\xe2\x82"),std::string("\xf0\x9f\x92")}) {
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
void quota_overflow() {
    bool rejected=false;
    try{(void)attachment::source_limit(std::numeric_limits<std::size_t>::max());}catch(const Error&){rejected=true;}
    check(rejected,"attachment quota padding must reject address-space overflow");
}
}
int main(){try{convention();quota_overflow();std::cout<<"Attachment application convention passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
