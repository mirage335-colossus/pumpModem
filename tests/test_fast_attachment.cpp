#include "datapump/fast/attachment.hpp"
#include "datapump/fast/compression.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace datapump;
using namespace datapump::fast;
namespace {
void check(bool value,const char* why){if(!value)throw Error(why);}
template<class F>void rejects(F action,const char* why){try{action();}catch(const Error&){return;}throw Error(why);}
Bytes bytes(std::string_view text){return Bytes(text.begin(),text.end());}
Bytes collect(SourceReader reader) {
    Bytes result;std::array<std::uint8_t,7> chunk{};
    while(const auto n=reader(chunk))result.insert(result.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(n));
    return result;
}
Bytes envelope(std::string_view name,const Bytes& payload) {
    auto result=bytes(attachment::prefix(name));result.insert(result.end(),payload.begin(),payload.end());return result;
}
std::shared_ptr<const ReceivedFile> receive(const Bytes& source,bool capacity,bool encrypted,
        std::uint64_t quota=1024*1024,bool raw=false,bool expect_success=true) {
    auto profile=capacity?capacity_profile(Channel::wire):classic_profile(Channel::wire);profile.interleave_depth=1;
    const auto key=encrypted?std::optional<Crypto>(Crypto(Bytes(32,93))):std::nullopt;
    auto encoded=raw?source:prepare_xz_source(byte_source(source),attachment::source_limit(quota)).encoded;
    const auto encoding=raw?SourceEncoding::raw:SourceEncoding::xz;
    StreamEncoder tx(profile,key,byte_source(encoded),encoding);
    StreamDecoder rx(profile,key,quota,encoding);
    std::array<std::uint8_t,physical_interval_bits> bits{};std::array<float,physical_interval_bits> soft{};
    while(tx.next_interval(bits)) {
        for(std::size_t i=0;i<bits.size();++i)soft[i]=bits[i]?12.f:-12.f;
        rx.push_interval(soft);
        check(!rx.result()&&!rx.snapshot().source_bytes&&!rx.snapshot().complete,"Attachment was interpreted before physical absence");
    }
    rx.finish(false);
    check(!rx.result()&&!rx.snapshot().source_bytes&&!rx.snapshot().physical_end,"EOF created attachment metadata/content");
    rx.finish(true);
    if(!expect_success) {
        check(rx.snapshot().failed&&!rx.snapshot().complete&&!rx.snapshot().source_bytes&&!rx.result(),
            "Invalid content quota published source/attachment metadata");return {};
    }
    check(rx.snapshot().complete&&rx.result(),"Valid completed source was rejected");
    check(rx.snapshot().source_bytes==rx.result()->size(),"Completed source count includes attachment prefix");
    return rx.result();
}
void syntax() {
    const std::string expected="#ATTACHMENT### filename.ext #ATTACHMENT### ";
    check(attachment::prefix("filename.ext")==expected,"Independent exact symmetric attachment vector changed");
    check(attachment::filename_from_path(std::filesystem::path("folder")/"filename.ext")=="filename.ext","Local path leaked into attachment basename");
    const Bytes payload{0,255,0,128,42,0};
    check(collect(attachment::source(byte_source(payload),"filename.ext",payload.size()))==envelope("filename.ext",payload),
        "Fragmented attachment source changed leading/trailing binary bytes");
    const auto exact=envelope("filename.ext",payload);
    const auto description=attachment::inspect(exact);
    check(description.filename=="filename.ext"&&description.prefix_bytes==expected.size(),"Exact leading prefix was not recognized");
    for(const auto leading:{std::string("x"),std::string("\n"),std::string("\xef\xbb\xbf"),std::string(1,'\0')}) {
        auto ordinary=bytes(leading);ordinary.insert(ordinary.end(),exact.begin(),exact.end());
        check(attachment::inspect(ordinary).prefix_bytes==0,"Nonleading attachment marker became an attachment");
    }
    for(std::size_t n=0;n<expected.size();++n)
        check(!attachment::inspect(std::span(exact).first(n)).prefix_bytes,"Truncated prefix became an attachment");
    check(!attachment::inspect(bytes("#ATTACHMENT### file.bin ###ATTACHMENT# data")).prefix_bytes,
        "Regular modem's asymmetric suffix leaked into Fast format");
    std::string utf8;for(unsigned i=0;i<127;++i)utf8+="\xc3\xa9";utf8+='x';
    check(attachment::inspect(bytes(attachment::prefix(utf8))).filename==utf8,"255-byte UTF-8 basename changed");
    rejects([&]{attachment::prefix(utf8+"x");},"256-byte filename accepted");
    for(const auto name:{std::string(""),std::string("."),std::string(".."),std::string("a/../b"),std::string("a\\b"),
                        std::string("C:x"),std::string("a\n"),std::string("a\0b",3),std::string("bad\xc0\x80"),
                        std::string("bad\xed\xa0\x80"),std::string("bad\xf4\x90\x80\x80"),std::string("a."),std::string("a "),
                        std::string("a #ATTACHMENT###"),std::string("a #ATTACHMENT### z")}) {
        rejects([&]{attachment::prefix(name);},"Invalid or ambiguous attachment basename accepted");
    }
    for(const auto name:{std::string(""),std::string("../x"),std::string("bad\xc0\x80"),std::string(256,'x')}) {
        const auto invalid=bytes(std::string(attachment::opening)+name+std::string(attachment::closing)+"body");
        check(!attachment::inspect(invalid).prefix_bytes,"Invalid received basename became an attachment");
    }
    rejects([&]{collect(attachment::source(byte_source(Bytes(4)),"a",3));},"Prefix reader bypassed content quota");
    rejects([&]{prepare_xz_attachment(byte_source(Bytes(4)),"a",3);},"Attachment compression bypassed content quota");
}
void completion_and_quotas() {
    const auto nested=envelope("inside.bin",Bytes{0,255,0});
    for(bool capacity:{false,true})for(bool encrypted:{false,true})for(const auto payload:{Bytes{},Bytes{0,255,0,128,0},nested}) {
        const auto original=envelope("caf\xc3\xa9.bin",payload);
        const auto result=receive(original,capacity,encrypted);
        check(result->is_attachment()&&result->filename()=="caf\xc3\xa9.bin"&&result->size()==payload.size()&&
            Bytes(result->bytes().begin(),result->bytes().end())==payload,"Completed attachment metadata/payload is not exact");
        const auto prepared=prepare_xz_attachment(byte_source(payload),"caf\xc3\xa9.bin",payload.size());
        check(prepared.source_bytes==payload.size()&&decode_xz(prepared.encoded,original.size())==original,
            "TX original-byte count or encoded envelope changed");
    }
    for(const auto ordinary:{bytes("hello"),bytes("before #ATTACHMENT### name.bin #ATTACHMENT### body"),
                            bytes("#ATTACHMENT### ../bad #ATTACHMENT### body"),bytes("#ATTACHMENT### broken"),
                            bytes("#ATTACHMENT### old.bin ###ATTACHMENT# body")}) {
        const auto result=receive(ordinary,true,false);
        check(!result->is_attachment()&&result->filename().empty()&&Bytes(result->bytes().begin(),result->bytes().end())==ordinary,
            "Ordinary or invalid-prefix data was changed/classified as an attachment");
    }
    const auto raw=envelope("raw.bin",Bytes{0,1,0});
    const auto literal=receive(raw,false,false,1024*1024,true);
    check(!literal->is_attachment()&&Bytes(literal->bytes().begin(),literal->bytes().end())==raw,"Raw wire-vector source interpretation changed");
    constexpr std::uint64_t quota=32768;
    const auto payload=Bytes(quota,'x');
    const auto maximum=envelope(std::string(255,'a'),payload);
    const auto result=receive(maximum,true,true,quota);
    check(result->is_attachment()&&result->size()==quota,"Bounded prefix reduced the accepted content quota");
    receive(envelope("file.bin",Bytes(quota+1,'x')),true,false,quota,false,false);
    receive(Bytes(quota+1,'x'),true,false,quota,false,false);
    auto invalid=bytes("#ATTACHMENT### ../bad #ATTACHMENT### ");invalid.resize(quota+1,'x');
    receive(invalid,true,true,quota,false,false);
    const auto path=std::filesystem::temp_directory_path()/("datapump-fast-envelope-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    result->save(path);std::ifstream input(path,std::ios::binary);Bytes saved(quota);input.read(reinterpret_cast<char*>(saved.data()),saved.size());
    check(saved==payload&&input.peek()==std::char_traits<char>::eof(),"Saved attachment includes envelope bytes");
    rejects([&]{result->save(path);},"Attachment save overwrote a destination");
}
}
int main(){try{syntax();completion_and_quotas();std::cout<<"Fast attachment prefix, metadata, quota and completion tests passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
