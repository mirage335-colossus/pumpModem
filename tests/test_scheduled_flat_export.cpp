#include "scheduled_flat_export.hpp"
#include <array>
#include <cfenv>
#include <iostream>
#include <stdexcept>
using namespace datapump::modem;
using namespace datapump::modem::detail;
void check(bool x,const char* s){if(!x)throw std::runtime_error(s);}
SchedHopLocation group_for(SchedHopCoordinate left,SchedHopCoordinate right) {
    SchedHopLocation g;g.active=true;g.flat_begin=left.sample-200;g.flat_end=right.sample+200;
    g.rise_begin=g.flat_begin-20;g.fall_end=g.flat_end+20;g.physical_begin=g.rise_begin-20;g.physical_end=g.fall_end+20;return g;
}
FhssPowerShape shape() {
    FhssPowerShape f;f.enabled=f.smooth=true;f.period=2400;f.quiet=100;f.support=20;f.lag_center=150;return f;
}
int main() {
    std::fesetround(FE_TONEAREST);int owner=0,other=0;const std::uint32_t Fs=6000;
    const SchedHopCoordinate epoch{12000000000000ULL,0};const auto fhss=shape();unsigned accepted=0;
    for(const long double rate:{.5L,1-0.0001L,1-3e-10L,1.L,1+3e-10L,1+0.0001L,2.L})
      for(unsigned trial=0;trial<128;++trial) {
        const auto first=std::uint64_t{10000}+trial%7,end=first+97-trial%11;
        const long double origin=3.125L+trial*.000003L;
        const auto left=epoch.add_signed((static_cast<long double>(first)-origin)*rate);
        const auto right=epoch.add_signed((static_cast<long double>(end)-origin)*rate);
        const auto group=group_for(left,right);
        const auto proof=scheduled_flat_export_certificate(&owner,origin,rate,7,first,end,left,right,group,fhss,Fs,true);
        if(!proof.valid())continue;
        ++accepted;
        check(proof.covers(&owner,origin,rate,7,first+1,end-1),"valid subset rejected");
        check(!proof.covers(&other,origin,rate,7,first,end) &&
              !proof.covers(&owner,origin,rate,8,first,end) &&
              !proof.covers(&owner,origin,rate,7,first-1,end),"identity/range mismatch accepted");
        unsigned cancellation_checks=0;
        for(auto n=first;n<end;++n) {
            if((n&31U)==0)++cancellation_checks;
            // ORIGINAL arbitrary <=128-sample piece reanchor. Carrier data,
            // source arithmetic and guards still use their original branches.
            const auto probe=first+(n-first)/15*15;
            const auto tick=epoch.add_signed((static_cast<long double>(probe)-origin)*rate);
            const auto coordinate=tick.add_signed(static_cast<long double>(n-probe)*rate);
            const auto g1=SchedHopSourceMap::gain(group,coordinate);
            const auto g2=fhss.gain(0,coordinate.sample%(2ULL*Fs)+coordinate.fraction,Fs);
            check(g1==1 && g2==1 && g1*g2==1,"certificate changed original literal gain1");
        }
        check(cancellation_checks>0,"sample cancellation cadence omitted");
        check(!scheduled_flat_export_certificate(&owner,origin,rate,7,first,end,left,right,group,fhss,Fs,false).valid(),"uncertified original family accepted");
        check(!scheduled_flat_export_certificate(&owner,origin,.4L,7,first,end,left,right,group,fhss,Fs,true).valid(),"unsupported rate accepted");
        check(!scheduled_flat_export_certificate(&owner,0x1p31L,rate,7,first,end,left,right,group,fhss,Fs,true).valid(),"unbounded origin accepted");
        check(!scheduled_flat_export_certificate(&owner,origin,rate,7,first,end,left,right,group,fhss,6001,true).valid(),"unqualified fractional period accepted");
    }
    check(accepted>100,"gain oracle admitted too few cases");
    // Exact and nextafter inward source boundaries; failure invalidates ONLY
    // the optional token, never the original phase-family admissibility.
    const auto base=epoch.sample;
    SchedHopLocation g;g.active=true;g.flat_begin=base+900;g.flat_end=base+1600;
    FhssPowerShape off;
    const SchedHopCoordinate a{base+902,0},b{base+1000,0};
    check(scheduled_flat_export_certificate(&owner,0,1,0,0,98,a,b,g,off,Fs,true).valid(),"two-sample source edge rejected");
    const auto before=std::nextafter(902.L,0.L);
    check(!scheduled_flat_export_certificate(&owner,0,1,0,0,98,{base+901,before-901},b,g,off,Fs,true).valid(),"inside source margin accepted");
    // Same test for original FHSS quiet/support edge and causal lag modulo.
    g.flat_begin=base+2200;g.flat_end=base+2600;
    const SchedHopCoordinate c{base+2372,0},d{base+2382,0};
    check(scheduled_flat_export_certificate(&owner,0,1,0,0,10,c,d,g,fhss,Fs,true).valid(),"two-sample FHSS edge rejected");
    const auto prior=std::nextafter(2372.L,0.L);
    check(!scheduled_flat_export_certificate(&owner,0,1,0,0,10,{base+2371,prior-2371},d,g,fhss,Fs,true).valid(),"inside FHSS margin accepted");
    const auto old_round=std::fegetround();std::fesetround(FE_UPWARD);
    const auto wrong_round=scheduled_flat_export_certificate(&owner,0,1,0,0,10,c,d,g,fhss,Fs,true);
    std::fesetround(old_round);check(!wrong_round.valid(),"unsupported fenv accepted");
    std::cout<<"flat source/FHSS literal-gain1, fractional/reanchor, identity, range and fallback oracles passed "<<accepted<<"cases\n";
}
