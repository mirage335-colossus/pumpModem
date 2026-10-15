#include "scheduled_pulse_derivative_chord_certificate.hpp"
#include <cfenv>
#include <iostream>
#include <stdexcept>

namespace m=datapump::modem;
namespace h=m::detail::scheduled_pulse_derivative_chord;
namespace original_h=m::detail::scheduled_pulse_chord;
namespace c=m::detail::scheduled_acquisition_certificate;
static std::size_t predicates=0;
#define CHECK(...) do {++predicates;if(!(__VA_ARGS__))throw std::runtime_error(#__VA_ARGS__);}while(false)
using Wide=std::complex<long double>;
struct Fixture {
    std::uint64_t symbol,chip;
    std::vector<h::Pair> coefficients;
    Fixture(std::uint64_t s,std::uint64_t q):symbol(s),chip(q),coefficients((s+q-1)/q) {
        for(std::size_t j=0;j<coefficients.size();++j)coefficients[j]={
            std::polar(.5+.2*std::sin(.17*j),.31*j),std::polar(.6+.1*std::cos(.11*j),-.23*double(j))};
    }
    h::Pair operator()(double sample,std::stop_token stop={})const {
        original_h::check_stop(stop);h::Pair result{};
        m::pattern_pulse_each(sample,symbol,chip,[&](std::uint64_t local,double p) {
            result[0]+=p*coefficients[local][0];result[1]+=p*coefficients[local][1];
        });return result;
    }
};
static h::Request request(const Fixture& f,double first,double last) {
    h::Request r;r.first=first;r.last=last;r.amplitude=.05;r.flat_gain=.83;r.coefficient_norm_upper=2;
    r.symbol_samples=f.symbol;r.chip_samples=f.chip;r.original_math_qualified=true;r.flat_gain_proven=true;
    r.workspace=h::DerivativeTable::reservation();return r;
}
static void paired(std::uint64_t chip,std::uint64_t tail,double first,double last,bool expect_jump,h::Execution mode) {
    Fixture f(40*chip+tail,chip);const auto r=request(f,first,last);
    const h::DerivativeTable table(chip,r.workspace,{},h::AggregateWidth::source_33_over_32);CHECK(table.valid());
    h::ExecutionReport report{mode};
    const auto result=h::certify(r,table,f,{},&report);
    if(mode==h::Execution::aggregate)CHECK(report.aggregate_used && !report.aggregate_fallback);
    CHECK(result.valid && result.failure==h::Failure::none);
    CHECK(result.evaluations==2 && result.points==2);
    CHECK(result.peak_temporary_bytes<=r.workspace);
    CHECK((result.support_edge_terms>0)==expect_jump);
    for(unsigned bit=0;bit<2;++bit) {
        const auto a=f(first)[bit],b=f(last)[bit];
        CHECK(result.left[bit]==r.flat_gain*m::pattern_limit_pcm(r.amplitude*a)/r.amplitude);
        CHECK(result.right[bit]==r.flat_gain*m::pattern_limit_pcm(r.amplitude*b)/r.amplitude);
    }
    // Dense deterministic points test the ORIGINAL executed producer against
    // the endpoint chord, including physical/source fractions near knots.
    const auto check_source=[&](double source) {
        const auto t=(static_cast<long double>(source)-first)/(static_cast<long double>(last)-first);
        const auto original=f(source);
        for(unsigned bit=0;bit<2;++bit) {
            const auto value=r.flat_gain*m::pattern_limit_pcm(r.amplitude*original[bit])/r.amplitude;
            const auto a=result.left[bit],b=result.right[bit];
            const Wide chord=(1-t)*Wide(a.real(),a.imag())+t*Wide(b.real(),b.imag());
            const auto error=std::abs(Wide(value.real(),value.imag())-chord);
            CHECK(error<=result.residual[bit]);
            CHECK(std::abs(Wide(value.real(),value.imag()))<=result.maximum_norm[bit]);
        }
    };
    for(unsigned i=0;i<=513;++i)check_source(first+(last-first)*(double(i)/513.));
    check_source(std::nextafter(first,last));check_source(std::nextafter(last,first));
    for(std::size_t j=0;j<f.coefficients.size();++j) {
        const auto duration=std::min(chip,f.symbol-j*chip);
        const auto center=double(j*chip)+.5*double(duration);
        for(const auto edge:{center-8.*chip,center+8.*chip}) {
            if(edge>first && edge<last) {
                check_source(edge);check_source(std::nextafter(edge,first));
                check_source(std::nextafter(edge,last));
            }
        }
    }
    // A closed finite-support edge is not made continuous by interpolation.
    if(expect_jump)CHECK(result.jump_radius>0);
    std::cout<<"C="<<chip<<" tail="<<tail<<" points="<<result.points<<" edges="<<result.support_edge_terms
        <<" residual0="<<result.residual[0]<<" residual1="<<result.residual[1]<<'\n';
}
static void table_checks() {
    for(const auto chip:{std::uint64_t{1},std::uint64_t{2},std::uint64_t{3},std::uint64_t{8},std::uint64_t{17},std::uint64_t{257},std::uint64_t{1}<<34}) {
        const h::DerivativeTable table(chip,h::DerivativeTable::reservation());CHECK(table.valid());
        const auto center_numerator=static_cast<std::int64_t>(256*20*chip+128*chip);
        for(int k=-2080;k<=2080;k+=31) {
            const auto first=static_cast<double>(center_numerator)/256.+(k+.3)*double(chip)/256.;
            const auto last=first+1.01;c::Interval range;CHECK(table.query(center_numerator,first,last,range));
            for(int i=-2048;i<2048;++i) {
                const auto left=static_cast<double>(center_numerator+static_cast<std::int64_t>(i)*static_cast<std::int64_t>(chip))/256.;
                const auto right=static_cast<double>(center_numerator+static_cast<std::int64_t>(i+1)*static_cast<std::int64_t>(chip))/256.;
                if(right<=first || left>=last)continue;
                const auto slope=(static_cast<long double>(m::pattern_pulse(double(i+1)/256.))-m::pattern_pulse(double(i)/256.))*256;
                CHECK(range.lo<=slope && slope<=range.hi);
            }
        }
    }
}
static void aggregate_checks() {
    for(const auto chip:{std::uint64_t{1},std::uint64_t{34},std::uint64_t{1}<<34})for(const auto width:{h::AggregateWidth::two_source_samples,h::AggregateWidth::source_33_over_32}) {
        const h::DerivativeTable table(chip,h::DerivativeTable::reservation(),{},width);CHECK(table.valid());
        for(const auto ratio:{.1,8.5,20.5,39.1})for(const auto fraction:{-.0001,0.,.0001}) {
            const auto a=ratio*chip+fraction,b=a+1.01;
            const auto lo=c::down(a-1e-12*chip),hi=c::up(b+1e-12*chip);
            double sum=0;std::size_t count=0;CHECK(table.aggregate_full(a,b,lo,hi,40,sum,count));
            std::size_t expected=0;long double actual_width_sum=0;
            for(std::uint64_t j=0;j<40;++j) {
                const auto center=(j+.5)*chip;
                for(const auto edge:{center-8.*chip,center+8.*chip})if(edge>=lo && edge<=hi)++expected;
                long double minimum=std::numeric_limits<long double>::infinity(),maximum=-minimum;
                for(int i=-2048;i<2048;++i) {
                    const auto left=center+double(i)*chip/256.,right=center+double(i+1)*chip/256.;
                    if(right<=a || left>=b)continue;
                    const auto slope=(static_cast<long double>(m::pattern_pulse(double(i+1)/256.))-m::pattern_pulse(double(i)/256.))*256;
                    minimum=std::min(minimum,slope);maximum=std::max(maximum,slope);
                }
                if(!std::isfinite(minimum)){minimum=maximum=0;}
                else if(a<center-8.*chip || b>center+8.*chip){minimum=std::min(minimum,0.L);maximum=std::max(maximum,0.L);}
                actual_width_sum+=maximum-minimum;
            }
            CHECK(count==expected);CHECK(actual_width_sum<=sum);
        }
        double untouched_sum=-1;std::size_t untouched_edges=999;
        CHECK(!table.aggregate_full(1,2,-100.*chip,100.*chip,40,untouched_sum,untouched_edges));
        CHECK(untouched_sum==-1 && untouched_edges==999);
    }
}
int main(){try {
    // Unsupported coordinate environments must retain runtime fallback rather
    // than make this optional local certificate a platform requirement.
    if(!c::coordinate_environment()) {
        Fixture f(320,8);CHECK(!h::certify(request(f,70.1,70.9),h::DerivativeTable(8,h::DerivativeTable::reservation()),f).valid);
        std::cout<<"unsupported chord coordinate producer; rejection checked\n";return 0;
    }
    table_checks();aggregate_checks();
    for(const auto mode:{h::Execution::reference,h::Execution::aggregate}) {
    paired(8,0,70.1,70.9,false,mode);
    paired(8,0,67.5,68.5,true,mode); // regular closed support edge at8.5C
    paired(17,3,545.0,546.0,true,mode); // final partial's left support edge:680+1.5−136=545.5
    paired(17,4,545.5,546.5,true,mode); // even partial center adds its own translated grid
    paired(1,0,9.1,10.09,true,mode);
    paired(34,0,301.1,302.11,false,mode);
    constexpr auto large_chip=std::uint64_t{1}<<34;
    paired(large_chip,3,8.5*large_chip-.25,8.5*large_chip+.25,true,mode);
    }
    {
        Fixture f(1360,34);auto r=request(f,301.1,303.1);
        const h::DerivativeTable narrow(34,r.workspace,{},h::AggregateWidth::source_33_over_32);
        CHECK(narrow.window_segments()==20 && narrow.aggregate_window_segments()==12);
        h::ExecutionReport report{h::Execution::aggregate};
        const auto fallback=h::certify(r,narrow,f,{},&report),reference=h::certify(r,narrow,f);
        CHECK(fallback.valid && reference.valid && report.aggregate_fallback && !report.aggregate_used);
        CHECK(fallback.residual==reference.residual && fallback.left==reference.left && fallback.right==reference.right);
        CHECK(fallback.evaluations==2);
    }
    Fixture f(320,8);auto r=request(f,70.1,70.9);
    const h::DerivativeTable table(8,r.workspace);CHECK(table.valid());
    CHECK(table.aggregate_window_segments()==0);
    h::ExecutionReport disabled_report{h::Execution::aggregate};
    const auto disabled=h::certify(r,table,f,{},&disabled_report),plain=h::certify(r,table,f);
    CHECK(disabled.valid && plain.valid && disabled_report.aggregate_fallback && !disabled_report.aggregate_used);
    CHECK(disabled.residual==plain.residual && disabled.left==plain.left && disabled.right==plain.right);
    const h::DerivativeTable wrong(9,r.workspace);CHECK(!h::certify(r,wrong,f).valid);
    r.flat_gain_proven=false;CHECK(!h::certify(r,table,f).valid);r.flat_gain_proven=true;
    r.original_math_qualified=false;CHECK(!h::certify(r,table,f).valid);r.original_math_qualified=true;
    r.last=320;CHECK(!h::certify(r,table,f).valid);r.last=70.9;
    r.workspace=1;CHECK(h::certify(r,table,f).failure==h::Failure::workspace);r.workspace=h::DerivativeTable::reservation();
    r.last=r.first+2.01;CHECK(!h::certify(r,table,f).valid);r.last=70.9;
    const h::DerivativeTable insufficient(8,1);CHECK(!insufficient.valid());
    c::Interval slopes;CHECK(!table.query(std::numeric_limits<std::int64_t>::max(),70.,71.,slopes));
    r.amplitude=1000;CHECK(h::certify(r,table,f).failure==h::Failure::clipping);r.amplitude=.05;
    r.amplitude=std::numeric_limits<double>::denorm_min();
    const auto tiny=h::certify(r,table,f);
    if(tiny.valid) {
        const auto source=.5*(r.first+r.last);
        const long double weight=(static_cast<long double>(source)-r.first)/(static_cast<long double>(r.last)-r.first);
        const auto actual=f(source);
        for(unsigned bit=0;bit<2;++bit) {
            const auto q=r.flat_gain*m::pattern_limit_pcm(r.amplitude*actual[bit])/r.amplitude;
            const Wide chord=(1-weight)*Wide(tiny.left[bit].real(),tiny.left[bit].imag())
                +weight*Wide(tiny.right[bit].real(),tiny.right[bit].imag());
            CHECK(std::abs(Wide(q.real(),q.imag())-chord)<=tiny.residual[bit]);
        }
    } else CHECK(tiny.failure==h::Failure::arithmetic || tiny.failure==h::Failure::clipping);
    r.amplitude=.05;
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)h::certify(r,table,f,stop.get_token());}catch(const std::exception&){cancelled=true;}CHECK(cancelled);
    const auto mode=std::fegetround();std::fesetround(FE_UPWARD);
    const auto refused=!h::certify(r,table,f).valid;std::fesetround(mode);CHECK(refused);
    std::cout<<"pulse derivative chord proposed predicates="<<predicates<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
