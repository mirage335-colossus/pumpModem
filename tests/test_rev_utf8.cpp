#include "../third_party/rev/external/rev_utf8.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        const std::string text="A\xc3\xa9\xe4\xb8\xad\xf0\x9f\x8c\x8dZ";
        std::vector<RevUtf8::Rune> runes;
        for(const auto rune:RevUtf8::runes(text)) runes.push_back(rune);
        check(runes.size()==5,"UTF-8 was counted as bytes instead of Unicode scalars");
        const char32_t values[]{U'A',0xe9,0x4e2d,0x1f30d,U'Z'};
        const std::size_t offsets[]{0,1,3,6,10,11};
        for(std::size_t i=0;i<runes.size();++i) {
            check(runes[i].value==values[i]&&runes[i].begin==offsets[i]&&runes[i].end==offsets[i+1],"Decoded scalar lost its original byte range");
            check(RevUtf8::previous(text,runes[i].end)==runes[i].begin,"Backspace boundary would split a multi-byte scalar");
            for(auto byte=runes[i].begin;byte<runes[i].end;++byte) check(RevUtf8::boundary(text,byte)==runes[i].begin,"Caret boundary splits UTF-8");
        }
        auto edited=text;
        const auto start=RevUtf8::previous(edited,10);
        edited.erase(start,10-start);
        check(edited=="A\xc3\xa9\xe4\xb8\xadZ","Deleting a four-byte glyph damaged adjacent text");
        for(const auto& invalid:std::vector<std::string>{"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x9f","\x80"}) {
            std::size_t consumed=0;
            for(const auto rune:RevUtf8::runes(invalid)) { check(rune.value==0xfffd&&rune.end==rune.begin+1,"Invalid UTF-8 did not make bounded replacement progress"); consumed=rune.end; }
            check(consumed==invalid.size(),"Invalid UTF-8 iterator stalled or dropped bytes");
        }
        check(RevUtf8::next({},0).end==0&&RevUtf8::previous({},99)==0,"Empty UTF-8 cursor handling failed");
        std::cout<<"Rev UTF-8 byte-boundary checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
