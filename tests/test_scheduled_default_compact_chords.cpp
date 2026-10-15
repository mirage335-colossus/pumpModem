#include "scheduled_default_compact_chords.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace compact=datapump::modem::detail::scheduled_default_compact_chords;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void fails(F&& f,const char* message){bool failed=false;try{f();}catch(const datapump::Error&){failed=true;}check(failed,message);}
struct Identity {std::uint64_t generation=7,source=13;bool operator==(const Identity&)const=default;};
bool same(const compact::Chord& a,const compact::Chord& b) {
    if(a.supported!=b.supported)return false;
    for(unsigned side=0;side<2;++side)for(unsigned bit=0;bit<2;++bit) {
        const auto x=(side?a.right:a.left)[bit],y=(side?b.right:b.left)[bit];
        if(std::bit_cast<std::uint64_t>(x.real())!=std::bit_cast<std::uint64_t>(y.real())||
           std::bit_cast<std::uint64_t>(x.imag())!=std::bit_cast<std::uint64_t>(y.imag()))return false;
    }
    return true;
}
compact::Chord chord(std::size_t k) {
    compact::Chord c;c.supported=true;
    switch(k%4) {
    case 0:c.supported=false;c.left[0]={std::numeric_limits<double>::quiet_NaN(),-0.};c.right[1]={-std::numeric_limits<double>::infinity(),1};break;
    case 1:break;
    case 2:c.left={compact::Pair::value_type{.001*(k+1),-.35},compact::Pair::value_type{.23,.19}};
        c.right={compact::Pair::value_type{std::numeric_limits<double>::denorm_min(),.31},compact::Pair::value_type{-.17,.27}};break;
    case 3:if(k%8==3)c.left[0]={-0.,0.};else c.right[1]={0.,-0.};break;
    }
    return c;
}
compact::Kind expected(std::size_t k) {
    constexpr std::array<compact::Kind,4> tags{compact::Kind::unsupported,compact::Kind::zero,compact::Kind::nonzero,compact::Kind::signed_zero};return tags[k%4];
}
void fill(compact::Directory& directory){for(std::size_t k=0;k<directory.size();++k)directory.append(chord(k));directory.seal();}
compact::Chord canonical(bool supported){compact::Chord c;c.supported=supported;return c;}
void packed_directory() {
    constexpr std::size_t cells=137;const auto bytes=compact::Directory::reservation(cells);compact::Directory directory(cells,bytes);
    check(directory.size()==cells&&directory.working_bytes()==bytes&&!directory.sealed(),"directory ownership/charge changed");
    fails([&]{(void)directory.kind(0);},"unsealed tags escaped");fill(directory);
    for(std::size_t k=0;k<cells;++k) {
        check(directory.kind(k)==expected(k)&&compact::classify(chord(k))==expected(k),"packed tag crossed word boundary");
        const auto c=directory.classification(k);
        check(c.supported==(k%4!=0)&&c.nonzero==(k%4==2),"classification promoted signed/positive zero");
    }
    fails([&]{(void)directory.kind(cells);},"out-of-range directory tag accepted");
    check(compact::Directory::reservation(31)==compact::Directory::reservation(32)&&
          compact::Directory::reservation(33)==compact::Directory::reservation(32)+8,"tags did not pack32 perword");
    const auto many=std::size_t{1000000};
    check(compact::Directory::reservation(many)-compact::Directory::reservation(32)==((many+31)/32-1)*8,"directory retained dense payload overhead");
    for(unsigned component=0;component<8;++component) {
        auto c=canonical(true);auto& z=(component<4?c.left:c.right)[(component%4)/2];
        if(component%2)z.imag(-0.);else z.real(-0.);
        check(compact::classify(c)==compact::Kind::signed_zero,"negative zero component collapsed");
    }
    auto tiny=canonical(true);tiny.right[1]={std::numeric_limits<double>::denorm_min(),0};
    check(compact::classify(tiny)==compact::Kind::nonzero,"subnormal payload became canonical zero");
}
void lazy_payload_tile() {
    compact::Directory directory(137,compact::Directory::reservation(137));fill(directory);const Identity id;
    compact::Tile<Identity> tile(id,compact::Tile<Identity>::reservation());std::vector<unsigned> calls(directory.size());
    const auto provider=[&](std::size_t k){check(expected(k)==compact::Kind::nonzero||expected(k)==compact::Kind::signed_zero,"provider resolved a canonical shortcut");++calls[k];return chord(k);};
    check(same(tile.get(directory,0,id,provider),canonical(false)),"unsupported payload not canonicalized");
    check(same(tile.get(directory,1,id,provider),canonical(true)),"positive zero shortcut changed bits");
    check(tile.stats.provider_calls==0&&tile.stats.shortcuts==2,"canonical shortcut used resolver");
    check(same(tile.get(directory,2,id,provider),chord(2))&&same(tile.get(directory,2,id,provider),chord(2)),"nonzero payload changed");
    check(same(tile.get(directory,3,id,provider),chord(3))&&same(tile.get(directory,3,id,provider),chord(3)),"signed zero payload changed");
    (void)tile.get(directory,62,id,provider);
    check(calls[2]==1&&calls[3]==1&&calls[62]==1&&tile.stats.provider_calls==3&&tile.stats.hits==2,"tile generated unrequested payload cells");
    for(std::size_t k=0;k<calls.size();++k)if(k!=2&&k!=3&&k!=62)check(calls[k]==0,"resolver eagerly generated fulltile");
    const auto retained=tile.get(directory,2,id,provider);(void)tile.get(directory,66,id,provider);
    check(same(retained,chord(2)),"returned value borrowed evicted tile");
    (void)tile.get(directory,2,id,provider);check(calls[2]==2,"tile change retained prior payload");
    (void)tile.get(directory,128,id,provider);(void)tile.get(directory,2,id,provider);
    check(calls[2]==3,"canonical request did not change/cleanse tile window");
    check(same(tile.get(directory,130,id,provider),chord(130))&&same(tile.get(directory,130,id,provider),chord(130)),"partial finaltile index changed");
    check(same(tile.get(directory,135,id,provider),chord(135))&&same(tile.get(directory,136,id,provider),canonical(false)),"partial tile boundary changed");
    check(tile.stats.requests==tile.stats.hits+tile.stats.provider_calls+tile.stats.shortcuts&&tile.stats.errors==0,"successful request statistics inconsistent");
    check(tile.working_bytes()==compact::Tile<Identity>::reservation()&&tile.working_bytes()<64*sizeof(compact::Chord)+4096,"tile retained unbounded payload storage");
}
void fallback_and_cancel() {
    compact::Directory directory(137,compact::Directory::reservation(137));fill(directory);const Identity id;
    compact::Tile<Identity> tile(id,compact::Tile<Identity>::reservation());unsigned calls=0;
    const auto good=[&](std::size_t k){++calls;return chord(k);};
    (void)tile.get(directory,2,id,good);auto wrong=id;++wrong.source;
    fails([&]{(void)tile.get(directory,2,wrong,good);},"changed exact identity reused payload");
    check(calls==1,"identity failure called provider");(void)tile.get(directory,2,id,good);check(calls==2,"identity failure did not clear tile");
    fails([&]{(void)tile.get(directory,137,id,good);},"out-of-range tile request accepted");
    const auto zero=[](std::size_t){return canonical(true);};
    fails([&]{(void)tile.get(directory,2,id,zero);},"resolver zero matched frozen nonzero");
    fails([&]{(void)tile.get(directory,3,id,zero);},"resolver erased signed zero");
    const auto invalid=[](std::size_t){auto c=canonical(true);c.left[0]={std::numeric_limits<double>::infinity(),0};return c;};
    fails([&]{(void)tile.get(directory,2,id,invalid);},"nonfinite supported resolver payload accepted");
    const auto unsupported=[](std::size_t){return canonical(false);};
    fails([&]{(void)tile.get(directory,2,id,unsupported);},"resolver unsupported matched frozen nonzero");
    (void)tile.get(directory,2,id,good);std::stop_source stop;
    const auto cancelling=[&](std::size_t k){stop.request_stop();return chord(k);};
    fails([&]{(void)tile.get(directory,6,id,cancelling,stop.get_token());},"provider cancellation published payload");
    const auto before=calls;(void)tile.get(directory,2,id,good);check(calls==before+1,"cancelled provider retained prior tile");
    fails([&]{(void)tile.get(directory,2,id,good,stop.get_token());},"cancelled cached hit published");
    (void)tile.get(directory,2,id,good);check(calls==before+2,"cancelled hit retained cache");
    bool threw=false;const auto throwing=[](std::size_t)->compact::Chord{throw std::runtime_error("provider fixture");};
    try{(void)tile.get(directory,6,id,throwing);}catch(const std::runtime_error&){threw=true;}
    check(threw&&tile.stats.errors>0,"provider exception was hidden");
    fails([&]{compact::Tile<Identity> denied(id,compact::Tile<Identity>::reservation()-1);},"tile workspace denial ignored");
    fails([&]{compact::Tile<Identity> cancelled(id,compact::Tile<Identity>::reservation(),stop.get_token());},"tile constructor cancellation ignored");
}
void invalid_directory() {
    fails([&]{(void)compact::Directory::reservation(0);},"empty directory accepted");
    fails([&]{(void)compact::Directory::reservation(compact::Directory::maximum_cells+1);},"directory extent cap ignored");
    fails([&]{compact::Directory d(65,compact::Directory::reservation(65)-1);},"directory preflight ignored");
    {
        compact::Directory d(3,compact::Directory::reservation(3));d.append(chord(0));
        fails([&]{d.seal();},"partial directory sealed");fails([&]{(void)d.kind(0);},"partial failure exposed tag");fails([&]{d.append(chord(1));},"failed directory resumed");
    }
    {
        compact::Directory d(1,compact::Directory::reservation(1));auto c=canonical(true);c.right[1]={0,std::numeric_limits<double>::quiet_NaN()};
        fails([&]{d.append(c);},"supported nonfinite append accepted");check(!d.sealed(),"invalid directory became sealed");
    }
    {
        compact::Directory d(1,compact::Directory::reservation(1));d.append(chord(0));d.seal();
        fails([&]{d.append(chord(1));},"sealed directory mutated");check(!d.sealed(),"bad mutation did not poison directory");
    }
    std::stop_source stop;stop.request_stop();fails([&]{compact::Directory d(3,compact::Directory::reservation(3),stop.get_token());},"directory constructor cancellation ignored");
    compact::Directory d(1,compact::Directory::reservation(1));d.append(chord(1));
    fails([&]{d.seal(stop.get_token());},"directory seal cancellation ignored");check(!d.sealed(),"cancelled directory published tags");
}
}
int main(){try{packed_directory();lazy_payload_tile();fallback_and_cancel();invalid_directory();
    std::cout<<"Default compact chord source cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
