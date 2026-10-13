#include "scheduled_outer_public_replay.hpp"
#include <iostream>
#include <vector>
using namespace datapump::modem::detail;
struct Pulse {std::uint64_t index;double weight;};
static void check(bool x){if(!x)throw std::runtime_error("outer public replay oracle failed");}
static void same(const CorrelationOuterEvidence& a,const CorrelationOuterEvidence& b) {
    check(ScheduledOuterPublicState::equal(a,b));check(a.dot==b.dot&&a.variance==b.variance&&a.contributing_terms==b.contributing_terms);
    for(unsigned bit=0;bit<2;++bit)check(a.score(bit)==b.score(bit)&&a.can_admit(bit,19)==b.can_admit(bit,19));
}
static std::array<std::complex<double>,2> coefficients(std::uint64_t i,unsigned lane) {
    return {std::polar(.7+.01*lane,.17*i+.23*lane),std::polar(.6+.03*lane,.31*i-.19*lane)};
}
static void direct(CorrelationOuterEvidence& x,std::span<const Pulse> row,std::complex<double> p,
                   unsigned lane,std::vector<std::uint64_t>& calls,unsigned fail=0) {
    for(const auto& pulse:row)x.add(pulse.index,pulse.weight*p,[&](std::uint64_t i) {
        calls.push_back(i);if(fail&&calls.size()==fail)throw std::runtime_error("injected coefficient failure");return coefficients(i,lane);
    });
}
int main() {
    for(bool initialized:{false,true}) {
        CorrelationOuterEvidence a,b;
        if(initialized) {
            std::array<Pulse,17> row;for(std::size_t i=0;i<17;++i)row[i]={34+i,.03*(i+1)};
            std::vector<std::uint64_t> ignored;direct(a,row,{.3,-.7},0,ignored);ScheduledOuterPublicState(a).copy_to(b);
        }
        // Incoming private accumulators are intentionally DIFFERENT.
        b.dot={std::complex<long double>{.2L,.3L},std::complex<long double>{-.4L,.7L}};b.variance={3,4};b.contributing_terms={2,3};
        auto ea=a,eb=b;ScheduledOuterPublicLockstep proof;check(proof.bind(&a,&b,17,true));
        for(unsigned sample=0;sample<4096;++sample) {
            std::array<Pulse,17> row;const auto first=initialized?35+sample/15:34+sample/15;
            for(std::size_t i=0;i<row.size();++i)row[i]={first+i,std::sin(.071*sample+.13*i)};
            const auto p=std::polar(.3,.317*sample);std::vector<std::uint64_t> ca,cb,ce,cf;
            check(ScheduledOuterPublicReplay::bounded(a,std::span<const Pulse>{row}));ScheduledOuterPublicReplay tape(a,ScheduledOuterPublicReplay::may_flush(a,std::span<const Pulse>{row}));
            tape.leader(a,std::span<const Pulse>{row},p,[&](std::uint64_t i){ca.push_back(i);return coefficients(i,0);});
            direct(ea,row,p,0,ce); // representative outer precedes its ORIGINAL bit Fits
            tape.follower(b,a,std::span<const Pulse>{row},p,[&](std::uint64_t i){cb.push_back(i);return coefficients(i,1);});
            direct(eb,row,p,1,cf);same(a,ea);same(b,eb);check(ca==ce&&cb==cf);
            check(proof.bind(&a,&b,17,true));
        }
        check(!proof.bind(&a,&b,18,true)); // target/private-context change breaks the piece certificate
    }
    // Every follower throw point must restore the SAME original public prefix.
    std::array<Pulse,17> row;for(std::size_t i=0;i<17;++i)row[i]={50+i,.013*(i+1)};
    for(bool initialized:{false,true}) {
    CorrelationOuterEvidence seed;
    if(initialized) {
        seed.initialized=true;seed.first=34;seed.last=50;
        for(std::size_t i=0;i<17;++i)seed.pulse[(34+i)%17]={.17*(i+1),-.23*(i+1)};
    }
    check(ScheduledOuterPublicReplay::bounded(seed,std::span<const Pulse>{row}));
    for(unsigned fail=1;fail<=16;++fail) {
        auto a=seed,b=seed;
        b.dot={std::complex<long double>{.2L,.3L},std::complex<long double>{-.4L,.7L}};b.variance={3,4};b.contributing_terms={2,3};
        auto original=b;ScheduledOuterPublicReplay tape(a);
        tape.leader(a,std::span<const Pulse>{row},{.3,.9},[&](std::uint64_t i){return coefficients(i,0);});
        check(tape.events()==16);std::vector<std::uint64_t> actual_calls,expected_calls;bool x=false,y=false;
        try{tape.follower(b,a,std::span<const Pulse>{row},{.3,.9},[&](std::uint64_t i) {
            actual_calls.push_back(i);if(actual_calls.size()==fail)throw std::runtime_error("injected coefficient failure");return coefficients(i,1);
        });}catch(const std::runtime_error&){x=true;}
        try{direct(original,row,{.3,.9},1,expected_calls,fail);}catch(const std::runtime_error&){y=true;}
        check(x&&y&&actual_calls==expected_calls);same(b,original);
    }
    }
    CorrelationOuterEvidence a,b;ScheduledOuterPublicLockstep proof;
    b.pulse[3]={-0.,0.};check(!proof.bind(&a,&b,17,true)); // signed zero is part of the public proof
    proof={};check(!proof.bind(&a,&a,17,true));proof={};check(!proof.bind(&a,nullptr,17,true));
    proof={};check(!proof.bind(&a,&b,17,false)); // trace/unsupported/insufficient frame fallback
    a.initialized=true;a.first=0;const std::array<Pulse,1> gap{{{1000,.5}}};
    check(!ScheduledOuterPublicReplay::bounded(a,std::span<const Pulse>{gap}));
    std::cout<<"8192 exact public replay rows, private callbacks/independent evidence and32 throw prefixes passed; tape_bytes="
        <<sizeof(ScheduledOuterPublicReplay)<<";frame_reserve="<<scheduled_outer_public_replay_frame<<'\n';
}
