#include "scheduled_owned_field_errors.hpp"
#include <array>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace o=d::scheduled_owned_moment_correction;
namespace e=d::scheduled_owned_field_errors;
using Complex=std::complex<double>;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool contains(c::Interval interval,double value){return interval.bounded() && interval.lo<=value && value<=interval.hi;}
o::Identity identity(){return {1,2,3,4,5,6};}
double square_norm(Complex value){return c::add(c::mul(value.real(),value.real()),c::mul(value.imag(),value.imag()));}
double difference_square(Complex a,Complex b) {
    const auto re=c::minus(c::Interval::point(a.real()),c::Interval::point(b.real()));
    const auto im=c::minus(c::Interval::point(a.imag()),c::Interval::point(b.imag()));
    return c::add(c::square(re).hi,c::square(im).hi);
}
void compare_fields(const o::Result& result,const std::array<d::CorrelationFit,2>& original) {
    check(result.valid,"Original field enclosure unsupported");
    for(unsigned bit=0;bit<2;++bit) {
        const auto& f=result.fields[bit];const auto& v=original[bit];
        check(f.count==v.count && f.count_lower==v.count && f.count_upper==v.count,"Original count changed");
        check(contains(f.xc,v.xc) && contains(f.xs,v.xs),"Original dot escaped bounds");
        check(contains(f.cc,v.cc) && contains(f.ss,v.ss) && contains(f.cs,v.cs),"Original Gram escaped bounds");
        check(contains(f.energy,v.energy),"Original PCM energy escaped bounds");
    }
}
struct Row {
    float pcm=0;Complex raw{},processing{};
    std::array<Complex,2> q0{},original{};
    bool selected=false;
};
struct Contracted {
    o::MathCell math;
    e::Bounds bounds;
    std::array<d::CorrelationFit,2> original{};
};
// Independent execution of the actual Original prefix subtraction and
// CorrelationFit::add association, including all unselected physical rows.
Contracted contract(const std::vector<Row>& rows,std::size_t prefix,std::uint64_t owner) {
    Contracted out;out.math.identity=identity();out.math.owner=owner;out.math.valid=true;
    auto& b=out.bounds;b.selected_energy_upper=b.union_energy_upper=0;
    b.q0_square_upper={};b.residual_square_upper={};b.original_qmax={};
    b.carrier_radius=b.carrier_magnitude=0;b.prefix_samples=prefix;
    std::array<o::Fields,2> fields{};std::uint64_t count=0;
    for(std::size_t first=0;first<rows.size();first+=prefix) {
        d::CorrelationProjection running;
        for(std::size_t i=first;i<std::min(rows.size(),first+prefix);++i) {
            const auto& row=rows[i];const auto x=static_cast<double>(row.pcm);
            const auto re=row.processing.real(),im=row.processing.imag();const auto previous=running;
            running.xc+=x*re;running.xs+=x*im;running.cc+=re*re;running.ss+=im*im;
            running.cs+=re*im;running.energy+=x*x;
            b.union_energy_upper=c::add(b.union_energy_upper,c::pcm_square(row.pcm).hi);
            b.carrier_magnitude=std::max({b.carrier_magnitude,c::norm(row.raw),c::norm(row.processing)});
            b.carrier_radius=std::max(b.carrier_radius,c::root(difference_square(row.processing,row.raw)));
            if(!row.selected)continue;
            ++count;b.selected_energy_upper=c::add(b.selected_energy_upper,c::pcm_square(row.pcm).hi);
            std::array<o::Fields,2> row_fields;
            check(o::row_fields({row.pcm,row.raw,row.q0,{}},row_fields),"stored row math unsupported");
            for(unsigned bit=0;bit<2;++bit) {
                fields[bit]=o::combine(fields[bit],row_fields[bit],true);
                b.q0_square_upper[bit]=c::add(b.q0_square_upper[bit],square_norm(row.q0[bit]));
                b.residual_square_upper[bit]=c::add(b.residual_square_upper[bit],difference_square(row.original[bit],row.q0[bit]));
                b.original_qmax[bit]=std::max(b.original_qmax[bit],c::norm(row.original[bit]));
                out.original[bit].add(running-previous,row.original[bit],1);
            }
        }
    }
    for(unsigned bit=0;bit<2;++bit) {
        const auto& f=fields[bit];
        out.math.fields[bit]={f.xc,f.xs,f.cc,f.ss,f.cs,f.energy,count,count,count,true,false};
    }
    return out;
}
void verify(Contracted fixture) {
    const auto count=fixture.original[0].count;
    const auto error=e::make(fixture.math.identity,fixture.math.owner,count,fixture.bounds,true);
    check(error.complete && !error.original_zero[0] && !error.original_zero[1],"incomplete or invented zero-basis proof");
    compare_fields(o::originalize(fixture.math,error),fixture.original);
}
void varying_rows_singleton_cell_and_whole() {
    std::vector<Row> rows(91);
    for(std::size_t i=0;i<rows.size();++i) {
        auto& row=rows[i];const auto n=static_cast<double>(i);
        row.pcm=static_cast<float>((static_cast<int>(i%13)-6)/16.);
        row.raw={.75+(i%3)/64.,.125+(i%5)/64.};
        row.processing={row.raw.real()+((i%2)?1e-7:-2e-7),row.raw.imag()+3e-7};
        row.q0={Complex{.125+n/1024.,-.25+n/2048.},Complex{-.375+n/4096.,.125-n/8192.}};
        for(unsigned bit=0;bit<2;++bit)
            row.original[bit]=row.q0[bit]+Complex{(bit?1:-1)*1e-5,(i%2?1:-1)*2e-6};
    }
    for(const auto count:{std::size_t{1},std::size_t{35},std::size_t{70}}) {
        for(auto& row:rows)row.selected=false;
        std::size_t selected=0;
        for(std::size_t i=3;i<rows.size() && selected<count;++i)if(i%5!=0){rows[i].selected=true;++selected;}
        check(selected==count,"bad count fixture");
        verify(contract(rows,17,count==70?o::no_owner:7));
    }
}
void large_unselected_prefix_cancellation() {
    std::vector<Row> rows(512);
    for(auto& row:rows){row.raw=row.processing={1,0};row.q0=row.original={Complex{1,0},Complex{0,1}};}
    rows.front().pcm=std::ldexp(1.0F,54);rows.back().pcm=3;rows.back().selected=true;
    auto fixture=contract(rows,512,9);
    check(fixture.original[0].xc==4 && fixture.original[0].energy==0,"prefix cancellation witness changed");
    check(fixture.bounds.selected_energy_upper>=9 && fixture.bounds.selected_energy_upper<10,"bad selected energy");
    auto selected_only=fixture.bounds;selected_only.union_energy_upper=selected_only.selected_energy_upper;
    const auto insufficient=e::make(identity(),9,1,selected_only,true);
    check(insufficient.complete && insufficient.fields[0].dot<.5,"fixture lost union-energy sensitivity");
    verify(fixture);
    rows.erase(rows.begin(),rows.end()-1);
    auto reset=contract(rows,1,9);
    check(reset.original[0].xc==3 && reset.original[0].energy==9,"prefix restart witness changed");
    verify(reset);
}
void underflow_and_zero_basis_are_not_inferred() {
    Row row;row.pcm=std::ldexp(1.0F,127);row.raw=row.processing={.5,0};row.selected=true;
    row.q0={Complex{},Complex{}};
    row.original={Complex{std::numeric_limits<double>::denorm_min(),0},Complex{std::ldexp(1.,-600),0}};
    auto fixture=contract(std::vector<Row>{row},1,5);
    check(fixture.original[0].xc>0,"Original prefix/phase underflow witness vanished");
    check(fixture.bounds.residual_square_upper[0]>0 && fixture.bounds.residual_square_upper[1]>0,"outward residual underflowed to zero");
    verify(fixture);
    row.pcm=.5F;row.q0={Complex{1,0},Complex{0,1}};row.original={};
    fixture=contract(std::vector<Row>{row},1,5);verify(fixture);
    const auto error=e::make(identity(),5,1,fixture.bounds,true);
    check(!error.original_zero[0] && !error.original_zero[1],"norm bound inferred Original exact zero");
}
// Compare to the conservative max-row/Cauchy version of RuntimeProxy's same
// template+carrier+prefix envelope. This is a bound comparison, not a timing test.
void runtime_style_envelope() {
    constexpr std::uint64_t count=35;constexpr std::size_t prefix=17;
    constexpr double q0max=1,emax=1e-6,m=2,a=1e-3;
    e::Bounds b;b.selected_energy_upper=count*.25;b.union_energy_upper=64;
    b.q0_square_upper={double(count),double(count)};
    b.residual_square_upper={c::mul(double(count),c::mul(emax,emax)),c::mul(double(count),c::mul(emax,emax))};
    b.original_qmax={c::add(q0max,emax),c::add(q0max,emax)};
    b.carrier_radius=a;b.carrier_magnitude=m;b.prefix_samples=prefix;
    const auto error=e::make(identity(),0,count,b,true);check(error.complete,"finite envelope unsupported");
    const auto n=static_cast<std::size_t>(count)+2;const auto length=static_cast<double>(n),q=b.original_qmax[0];
    const auto u=std::numeric_limits<double>::epsilon()/2;
    const auto rho=c::add(c::gamma(128*n+128*prefix+128,u),c::mul(4*prefix,c::gamma(8*prefix+8,u)));
    const auto root=c::root(c::mul(length,b.union_energy_upper));
    const auto template_dot=c::mul(c::mul(m,emax),c::root(c::mul(double(count),b.selected_energy_upper)));
    const auto template_gram=c::mul(c::mul(double(count),c::mul(m,m)),c::add(c::mul(c::mul(2,q0max),emax),c::mul(emax,emax)));
    const auto carrier_dot=c::mul(c::mul(a,q),root);
    const auto carrier_gram=c::mul(c::mul(length,c::mul(q,q)),c::add(c::mul(c::mul(2,m),a),c::mul(a,a)));
    const auto amplify=c::add(1,c::gamma(12*n+32,u));
    const auto tiny=c::mul(c::mul(128*length*(prefix+1)+128,std::numeric_limits<double>::denorm_min()),
        c::add(1,c::add(q,c::add(c::mul(q,q),c::root(b.union_energy_upper)))));
    const auto old_dot=c::add(c::mul(amplify,c::add(template_dot,c::add(carrier_dot,c::mul(c::mul(8*rho,q),root)))),tiny);
    const auto old_gram=c::add(c::mul(amplify,c::add(template_gram,c::add(carrier_gram,c::mul(c::mul(32*rho,length),c::mul(q,q))))),tiny);
    check(error.fields[0].dot<=old_dot*(1+1e-12) && error.fields[0].gram<=old_gram*(1+1e-12),"L2 bound exceeds max-row envelope");
}
void caps_identity_environment_and_empty() {
    e::Bounds b;b.selected_energy_upper=b.union_energy_upper=0;b.q0_square_upper={};
    b.residual_square_upper={};b.original_qmax={};b.carrier_radius=0;b.carrier_magnitude=1;b.prefix_samples=1;
    const auto empty=e::make(identity(),o::no_owner,0,b,true);
    check(empty.complete && empty.fields[0].valid && empty.fields[0].dot==0 && empty.fields[1].energy==0 &&
        !empty.original_zero[0] && !empty.original_zero[1],"empty contract wrong");
    check(e::make(identity(),0,e::maximum_count,b,true).complete,"maximum count unsupported");
    check(!e::make(identity(),0,e::maximum_count+1,b,true).complete,"overlarge count admitted");
    check(!e::make(identity(),0,1,b,false).complete,"incomplete semantic proof admitted");
    auto bad_identity=identity();bad_identity.ledger=0;
    check(!e::make(bad_identity,0,1,b,true).complete,"missing ledger admitted");
    auto bad=b;bad.prefix_samples=0;check(!e::make(identity(),0,0,bad,true).complete,"zero prefix admitted");
    bad=b;bad.prefix_samples=513;check(!e::make(identity(),0,1,bad,true).complete,"oversize prefix admitted");
    bad=b;bad.carrier_magnitude=c::up(2);check(!e::make(identity(),0,1,bad,true).complete,"oversize carrier admitted");
    bad=b;bad.residual_square_upper[1]=-1;check(!e::make(identity(),0,1,bad,true).complete,"negative residual admitted");
    bad=b;bad.original_qmax[0]=std::numeric_limits<double>::quiet_NaN();check(!e::make(identity(),0,1,bad,true).complete,"NaN admitted");
    bad=b;bad.selected_energy_upper=1;check(e::make(identity(),0,1,bad,true).complete,"loose selected upper bound rejected");
    bad=b;bad.original_qmax[0]=std::numeric_limits<double>::max();check(!e::make(identity(),0,1,bad,true).complete,"overflowing radii admitted");
    const auto mode=std::fegetround();check(std::fesetround(FE_DOWNWARD)==0,"cannot set rounding fixture");
    const auto wrong_mode=e::make(identity(),0,1,b,true);const auto restored=std::fesetround(mode);
    check(restored==0 && !wrong_mode.complete,"nonnearest environment admitted");
}
int main() {
    if(!c::environment<double>()){std::cerr<<"qualified binary64 environment unavailable\n";return 77;}
    try {
        varying_rows_singleton_cell_and_whole();large_unselected_prefix_cancellation();
        underflow_and_zero_basis_are_not_inferred();runtime_style_envelope();caps_identity_environment_and_empty();
        std::cout<<"owned field errors: passed\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
