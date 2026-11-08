#include "datapump/stream_codec.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) {
    try { action(); } catch(const Error&) { return; }
    throw std::runtime_error(message);
}
Bytes noise(std::size_t count,std::mt19937& random) {
    Bytes result(count);for(auto& byte:result)byte=static_cast<std::uint8_t>(random());return result;
}
void generic_rs() {
    std::mt19937 random(0x199b7);
    for(const auto parity:{2U,4U,22U,48U,90U}) {
        const auto pristine=fec::rs_encode(noise(255-parity,random),parity);
        for(unsigned trial=0;trial<70;++trial) {
            const auto erasures=trial%(parity+1),errors=(parity-erasures)/2;
            std::vector<std::size_t> positions(pristine.size());std::iota(positions.begin(),positions.end(),0);
            std::shuffle(positions.begin(),positions.end(),random);auto damaged=pristine;
            for(std::size_t i=0;i<erasures+errors;++i)damaged[positions[i]]^=static_cast<std::uint8_t>(1+random()%255);
            const auto changed=fec::rs_correct(damaged,parity,std::span(positions).first(erasures));
            check(changed==erasures+errors && damaged==pristine,"mixed errors/erasures at bound must repair exactly");
        }
        std::vector<std::size_t> erased(parity+1);std::iota(erased.begin(),erased.end(),0);
        auto unchanged=pristine;
        rejects([&]{fec::rs_correct(unchanged,parity,erased);},"too many erasures rejected even for zero syndrome");
        check(unchanged==pristine,"invalid erasures leave codeword unchanged");
        erased={0,0};rejects([&]{fec::rs_correct(unchanged,parity,erased);},"duplicate erasures rejected");
        erased={pristine.size()};rejects([&]{fec::rs_correct(unchanged,parity,erased);},"out-of-range erasure rejected");
        for(const auto position:{std::size_t{255},std::size_t{256},std::numeric_limits<std::size_t>::max()}) {
            erased={position};
            rejects([&]{fec::rs_correct(unchanged,parity,erased);},"large erasure index must not be clamped into acceptance");
            check(unchanged==pristine,"rejected large erasure index leaves codeword unchanged");
        }
    }
    // A known erasure may already contain its true zero value. It still uses
    // one erasure equation, and cannot be silently removed from the budget.
    auto zero=fec::rs_encode(Bytes(106),22);std::vector<std::size_t> erased(22);std::iota(erased.begin(),erased.end(),0);
    check(fec::rs_correct(zero,22,erased)==0,"valid zero-valued erasures need no changed values");
    for(const auto length:{std::size_t{0},std::size_t{1},std::size_t{256}}) {
        Bytes invalid(length);
        rejects([&]{fec::rs_correct(invalid,1);},"invalid RS word width rejected before scratch access");
    }
    for(const auto parity:{std::size_t{0},std::size_t{128},std::numeric_limits<std::size_t>::max()}) {
        auto invalid=zero;
        rejects([&]{fec::rs_correct(invalid,parity);},"invalid RS parity width rejected before scratch access");
        check(invalid==zero,"invalid RS dimensions do not mutate source");
    }
}
} // namespace
int main() {
    try { generic_rs();std::cout<<"shared Reed-Solomon codec passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
