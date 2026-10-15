#include "scheduled_sparse_carrier.hpp"
#include "scheduled_owned_moment_correction.hpp"
#include <array>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace d=datapump::modem::detail;
namespace s=d::scheduled_sparse_carrier;
namespace o=d::scheduled_owned_moment_correction;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
using Complex=std::complex<double>;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void contains(c::Interval i,double value,const char* message){check(i.bounded() && i.lo<=value && value<=i.hi,message);}
Complex sequential(const s::Fence& fence,std::size_t offset){auto value=fence.seed;for(std::size_t i=0;i<offset;++i)value*=fence.step;return value;}
void ball_contains(const s::Point& point,Complex actual){
    const auto real=static_cast<long double>(actual.real())-point.center.real();
    const auto imag=static_cast<long double>(actual.imag())-point.center.imag();
    check(point.valid && std::sqrt(real*real+imag*imag)<=static_cast<long double>(point.radius),"sequential raw carrier escaped fast ball");
}
s::Fence fence(Complex seed,Complex step){return {1,2,3,100,512,seed,step};}
void independent_sequential_recurrences(){
    const std::array fences{
        fence({.6,.8},std::polar(1.,.319)),
        fence({1,1e-200},{std::sqrt(.5),std::sqrt(.5)}),
        fence({-.8,.3},{1.00009,-.00001}),
        fence({.37,-.81},{-.9999,.0003})};
    for(const auto& f:fences){auto actual=f.seed;
        for(std::size_t n=0;n<512;++n){
            const auto point=s::carrier_at(f,n);ball_contains(point,actual);
            check(s::carrier_point_matches(point,f,100+n),"actual raw fence provenance lost");
            if(!n)check(point.radius==0 && point.center==f.seed,"zero offset changed raw seed");
            actual*=f.step;
        }
    }
}
void subnormal_cancellation_and_invalid_domains(){
    constexpr auto eta=std::numeric_limits<double>::denorm_min();
    const std::array fences{fence({eta,eta},{.5,.5}),fence({1e-300,-1e-300},{.25,-.5}),
        fence({1,-1},{1-std::numeric_limits<double>::epsilon(),eta}),fence({0,-0.},{0,0})};
    for(const auto& f:fences)for(auto n:{0U,1U,2U,7U,255U,511U})ball_contains(s::carrier_at(f,n),sequential(f,n));
    auto f=fence({.6,.8},std::polar(1.,.7));
    check(!s::carrier_at(f,512).valid,"offset512 admitted");
    auto bad=f;bad.ledger=0;check(!s::carrier_at(bad,1).valid,"unknown raw ledger admitted");
    bad=f;bad.step={1.01,0};check(!s::carrier_at(bad,1).valid,"unsupported step growth admitted");
    bad=f;bad.seed={3,0};check(!s::carrier_at(bad,1).valid,"unsupported seed magnitude admitted");
    bad=f;bad.seed={std::numeric_limits<double>::quiet_NaN(),0};check(!s::carrier_at(bad,1).valid,"NaN seed admitted");
    bad=f;bad.first=std::numeric_limits<std::uint64_t>::max()-100;check(!s::carrier_at(bad,1).valid,"raw coordinate overflow admitted");
    const auto point=s::carrier_at(f,5);bad=f;bad.step.real(std::nextafter(f.step.real(),0));
    check(!s::carrier_point_matches(point,bad,105),"foreign raw step bits admitted");
    check(!s::carrier_point_matches(point,f,106),"foreign physical row admitted");
    const auto old=std::fegetround();check(std::fesetround(FE_UPWARD)==0,"could not change rounding mode");
    const auto invalid=s::carrier_at(f,1);check(std::fesetround(old)==0 && !invalid.valid,"non-RN carrier certified");
}
o::Identity identity(){return {1,2,3,4,5,6};}
o::FinalErrors final_errors(std::uint64_t owner,std::uint64_t count){return {identity(),owner,count,{{{0,0,0,0,true},{0,0,0,0,true}}},{},true};}
void check_fields(const g::FitIntervals& f,const d::CorrelationFit& actual){
    check(g::finite(f) && f.count==actual.count && f.count_lower==actual.count && f.count_upper==actual.count,"corrected count mismatch");
    contains(f.xc,actual.xc,"raw carrier correction xc escaped");contains(f.xs,actual.xs,"raw carrier correction xs escaped");
    contains(f.cc,actual.cc,"raw carrier correction cc escaped");contains(f.ss,actual.ss,"raw carrier correction ss escaped");
    contains(f.cs,actual.cs,"raw carrier correction cs escaped");contains(f.energy,actual.energy,"raw carrier correction energy escaped");
}
void signed_raw_carrier_support_corrections(){
    const auto raw=fence({.6,.8},std::polar(1.,.319));const auto id=identity();
    std::array<o::RowSample,3> exact,approximate;
    for(std::size_t n=0;n<3;++n){
        const auto offset=509+n;const auto carrier=sequential(raw,offset);const auto point=s::carrier_at(raw,offset);
        // Legacy four-field aggregate remains valid with appended default0.
        exact[n]={static_cast<float>(.2+.1*n),carrier,{{{.3,.2},{-.1,.15}}},{{0,0}}};
        approximate[n]=exact[n];approximate[n].oscillator=point.center;approximate[n].oscillator_radius=point.radius;
        check(exact[n].oscillator_radius==0,"legacy aggregate lost radius0 default");
    }
    auto native=o::empty_native(id,0);
    for(std::size_t n=0;n<2;++n){std::array<o::Fields,2> row;check(o::row_fields(exact[n],row),"exact raw row unavailable");
        for(unsigned bit=0;bit<2;++bit){const auto v=o::combine(o::fields(native.fields[bit]),row[bit],true);const auto count=n+1;
            native.fields[bit]={v.xc,v.xs,v.cc,v.ss,v.cs,v.energy,count,count,count,true,false};}
    }
    // Same physical row at chip+mask boundary appears once: two departures,
    // a forward chip transfer and a native0 window entrant. Whole loses one
    // native row and gains one row; transfer changes only cell ownership.
    const std::array<o::RowEdit,3> edits{{{id,609,0,o::no_owner,approximate[0]},
        {id,610,0,1,approximate[1]},{id,611,o::no_owner,1,approximate[2]}}};
    const std::array<o::OwnerCount,2> owners{{{0,2,0},{1,0,2}}};
    const o::SupportProof proof{id,2,2,true,true};std::array<o::OwnerDelta,2> delta;
    check(o::build_owner_deltas(proof,owners,edits,delta),"fast raw support edits invalid");
    const auto empty=o::corrected_cell(native,delta[0],final_errors(0,0));
    check(empty.valid && o::empty_fields(empty.fields[0]),"departed native owner not empty");
    const auto entrant=o::corrected_cell(o::empty_native(id,1),delta[1],final_errors(1,2));
    check(entrant.valid,"fast native0 entrant correction invalid");
    std::array<d::CorrelationFit,2> actual;
    for(std::size_t n=1;n<3;++n){const auto& r=exact[n];const auto x=static_cast<double>(r.pcm),a=r.oscillator.real(),b=r.oscillator.imag();
        const d::CorrelationProjection projection{x*a,x*b,a*a,b*b,a*b,x*x};
        for(unsigned bit=0;bit<2;++bit)actual[bit].add(projection,r.q0[bit],1);
    }
    for(unsigned bit=0;bit<2;++bit)check_fields(entrant.fields[bit],actual[bit]);
    o::WholeDelta whole;check(o::build_whole_delta(proof,edits,whole) && whole.removed==1 && whole.added==1,"whole double-counted transfer");
    native.owner=o::no_owner;
    const auto corrected=o::corrected_whole(native,whole,final_errors(o::no_owner,2));
    check(corrected.valid,"fast carrier whole correction invalid");for(unsigned bit=0;bit<2;++bit)check_fields(corrected.fields[bit],actual[bit]);
}
void radius_fields_and_template_error(){
    const auto raw=fence({.6,.8},std::polar(1.,.319));const auto point=s::carrier_at(raw,511);const auto actual=sequential(raw,511);
    o::RowSample row{.7F,point.center,{{{.3,.2},{-.1,.15}}},{{1e-6,1e-6}},point.radius};
    const auto errors=o::row_template_errors(row);check(errors[0].valid && errors[1].valid,"radius-aware template error unavailable");
    auto enlarged=row;enlarged.oscillator_radius=.125;
    auto exact_center=row;exact_center.oscillator_radius=0;
    const auto enlarged_errors=o::row_template_errors(enlarged),center_errors=o::row_template_errors(exact_center);
    check(enlarged_errors[0].dot>center_errors[0].dot && enlarged_errors[0].gram>center_errors[0].gram,"template error omitted carrier disk");
    std::array<o::Fields,2> fields;check(o::row_fields(row,fields),"carrier disk fields invalid");
    const auto q=row.q0[0]*actual;const auto x=static_cast<double>(row.pcm);
    contains(fields[0].xc,x*q.real(),"carrier disk missed raw xc");contains(fields[0].ss,q.imag()*q.imag(),"carrier disk missed raw ss");
    row.oscillator_radius=-1;check(!o::row_fields(row,fields),"negative carrier radius admitted");
    row.oscillator_radius=c::infinity;check(!o::row_fields(row,fields),"unknown carrier radius admitted");
}
int main(){try{
    independent_sequential_recurrences();subnormal_cancellation_and_invalid_domains();
    signed_raw_carrier_support_corrections();radius_fields_and_template_error();
    std::cout<<"sparse raw carrier: four focused cases passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
