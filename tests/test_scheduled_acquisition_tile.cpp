#include "scheduled_acquisition_tile.hpp"
#include "datapump/pattern_pulse.hpp"
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

using namespace datapump::modem;
using datapump::Error;
namespace d=datapump::modem::detail;
namespace tile=d::scheduled_acquisition_tile;
using C=std::complex<double>;
#define CHECK(condition) do {if(!(condition))throw std::runtime_error("check failed: " #condition);} while(false)
constexpr std::size_t budget=32*1024*1024;
constexpr double tau=2*std::numbers::pi;

bool near(double a,double b,double relative,double absolute) {
    return std::abs(a-b)<=absolute+relative*std::max(std::abs(a),std::abs(b));
}
void compare(const d::CorrelationFit& a,const d::CorrelationFit& b,double tolerance,double absolute=1e-8) {
    CHECK(a.count==b.count);
    CHECK(near(a.energy,b.energy,2e-12,absolute));
    CHECK(near(a.xc,b.xc,tolerance,absolute));CHECK(near(a.xs,b.xs,tolerance,absolute));
    CHECK(near(a.cc,b.cc,tolerance,absolute));CHECK(near(a.ss,b.ss,tolerance,absolute));
    CHECK(near(a.cs,b.cs,tolerance,absolute));
}
template<class F>void rejects(F function) {
    bool rejected=false;try{function();}catch(const Error&){rejected=true;}CHECK(rejected);
}
d::CorrelationFit original(const tile::Input& input,const tile::Fragment& fragment,std::size_t start,unsigned bit) {
    d::CorrelationFit fit;
    for(std::size_t i=0;i<fragment.rows.size();++i) {
        const auto n=start+i;
        if(!tile::selected(fragment,i) || !tile::present(input,n))continue;
        const auto x=static_cast<double>(input.pcm[n]);const auto o=input.oscillator[n];
        fit.add({x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x},fragment.rows[i][bit],1);
    }
    return fit;
}
void merge(d::CorrelationFit& a,const d::CorrelationFit& b) {
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
}

struct Fixture {
    std::vector<float> pcm;
    std::vector<C> oscillator;
    std::vector<std::uint8_t> observed,mask;
    std::vector<std::array<C,2>> rows;
    std::uint64_t offset=0,origin=0;
    tile::Input input()const{return {pcm,oscillator,observed,origin+offset};}
    tile::Fragment fragment()const{return {rows,mask,offset};}
};
Fixture fixture(tile::Shape shape,std::size_t length,std::uint64_t origin,std::uint64_t offset,
                double frequency,unsigned seed,bool clipped=false) {
    Fixture f;f.origin=origin;f.offset=offset;
    const auto samples=shape.start_span+shape.maximum_fragment-1;
    f.pcm.resize(samples);f.oscillator.resize(samples);f.observed.assign(samples,1);
    f.rows.resize(length);f.mask.assign(length,1);
    std::mt19937_64 rng(seed);std::normal_distribution<double> normal;
    for(std::size_t i=0;i<samples;++i) {
        auto x=normal(rng)*.31;
        if(clipped)x=std::clamp(7*x+.7*std::sin(.073*i),-.99,.99);
        f.pcm[i]=static_cast<float>(x);
        const auto position=static_cast<long double>(origin)+offset+i;
        const auto angle=static_cast<double>(std::remainder(position*tau*frequency/6120,static_cast<long double>(tau)));
        // Retain a small original recurrence-norm deviation as well as phase.
        f.oscillator[i]=std::polar(1.+1e-12*i,angle);
        if(i%31==0)f.observed[i]=0;
    }
    std::array<std::array<C,2>,96> coefficients{};
    for(auto& pair:coefficients)for(auto& c:pair)c={normal(rng)*.4,normal(rng)*.4};
    for(std::size_t i=0;i<length;++i) {
        // Original finite pulse table at fractional/source-clock coordinates;
        // the tile receives exact sampled rows, never their interpolation.
        const auto within=34*17+.137+i*(1.+(seed%3-1.)*1e-5);
        for(unsigned bit=0;bit<2;++bit) {
            const auto shaped=pattern_pulse_sum(within,96*34-13,34,
                [&](std::uint64_t chip){return coefficients[chip][bit];});
            const auto gain=.15+.85*std::pow(std::sin(std::numbers::pi*(i+.5)/length),2);
            f.rows[i][bit]=gain*pattern_limit_pcm(.2766992952647332*shaped)/.2766992952647332;
        }
        if(i%19==0)f.mask[i]=0;
        if(i%23==0)f.rows[i]={C{},C{}}; // Active zeros retain physical noise/count.
    }
    return f;
}

template<class T>void paired_fragments() {
    constexpr double tolerance=std::is_same_v<T,float>?5e-5:2e-10;
    for(std::size_t maximum:{1U,2U,17U,34U,257U})for(double frequency:{.005,.5,1500.}) {
        const tile::Shape shape{37,maximum};tile::Plan<T> plan(shape);
        const std::array<std::size_t,5> starts{0,1,8,19,36};
        constexpr std::uint64_t origin=std::uint64_t{1}<<52;
        tile::Accumulator<T> accumulator(plan,origin,starts,budget);
        std::vector<tile::Value> direct(starts.size()),truth(starts.size());
        std::size_t aggregate_bytes=plan.working_bytes()+accumulator.working_bytes();
        // Physical phase advances through both a short and an enormous null
        // interval, while all allocations depend only on bounded tile sizes.
        for(std::size_t k=0;k<3;++k) {
            const std::uint64_t offset=k==0?0:k==1?40000:std::uint64_t{1}<<41;
            const auto length=maximum>k?maximum-k:1;
            auto f=fixture(shape,length,origin,offset,frequency,89123+static_cast<unsigned>(k),k==2);
            tile::PreparedInput<T> input(plan,f.input(),budget);
            tile::PreparedFragment<T> fragment(plan,f.fragment(),budget);
            CHECK(input.work.input_transforms==3);CHECK(fragment.work.template_transforms==6);
            aggregate_bytes=std::max(aggregate_bytes,plan.working_bytes()+accumulator.working_bytes()+
                input.working_bytes()+fragment.working_bytes());
            accumulator.add(input,fragment);
            tile::direct_add(f.input(),f.fragment(),origin,starts,direct);
            for(std::size_t at=0;at<starts.size();++at)for(unsigned bit=0;bit<2;++bit)
                merge(truth[at].fits[bit],original(f.input(),f.fragment(),starts[at],bit));
        }
        const auto output=accumulator.finish();
        CHECK(output.size()==starts.size());CHECK(accumulator.work().inverse_transforms==6);
        CHECK(accumulator.work().fragments==3);CHECK(aggregate_bytes<budget);
        for(std::size_t at=0;at<starts.size();++at) {
            CHECK(output[at].physical_origin==origin+starts[at]);
            for(unsigned bit=0;bit<2;++bit) {
                compare(output[at].fits[bit],truth[at].fits[bit],tolerance,2e-5);
                compare(direct[at].fits[bit],truth[at].fits[bit],2e-12,1e-9);
                if(!output[at].requires_refinement[bit])
                    CHECK(near(output[at].fits[bit].score(),truth[at].fits[bit].score(),5e-4,2e-4));
            }
        }
        rejects([&]{accumulator.finish();});
    }
}

void hand_oracles() {
    const tile::Shape shape{1,2};tile::Plan<double> plan(shape);
    std::vector<float> pcm{3,5};std::vector<C> oscillator(2,C{1,0});
    std::vector<std::array<C,2>> rows{{{{1,1},{2,0}}},{{{2,-1},{1,1}}}};
    const std::array<std::size_t,1> starts{0};
    {
        tile::PreparedInput<double> input(plan,{pcm,oscillator,{},1000},budget);
        tile::PreparedFragment<double> fragment(plan,{rows,{},0},budget);
        tile::Accumulator<double> accumulator(plan,1000,starts,budget);accumulator.add(input,fragment);
        const auto output=accumulator.finish();
        compare(output[0].fits[0],{13,-2,5,2,-1,34,2},1e-12);
        compare(output[0].fits[1],{11,5,5,1,1,34,2},1e-12);
    }

    const tile::Shape longer{1,3};tile::Plan<float> fp(longer);
    pcm={2,99,3};oscillator.assign(3,{1,0});
    rows={{{{1,0},{1,0}}},{{{0,0},{0,0}}},{{{0,1},{0,1}}}};
    std::vector<std::uint8_t> mask{1,1,1};
    tile::PreparedInput<float> pi(fp,{pcm,oscillator,{},7},budget);
    tile::PreparedFragment<float> pf(fp,{rows,mask,0},budget);
    tile::Accumulator<float> ac(fp,7,starts,budget);ac.add(pi,pf);
    const auto result=ac.finish();CHECK(result[0].fits[0].count==3);
    CHECK(near(result[0].fits[0].energy,9814,1e-12,1e-10));
    auto missing_mask=mask;missing_mask[1]=0;
    tile::PreparedFragment<float> missing(fp,{rows,missing_mask,0},budget);
    tile::Accumulator<float> ac2(fp,7,starts,budget);ac2.add(pi,missing);
    CHECK(ac2.finish()[0].fits[0].count==2);
}

template<class T>void one_sample_and_partial_chip() {
    const double tolerance=std::is_same_v<T,float>?5e-5:2e-10;
    {
        const tile::Shape shape{9,1};tile::Plan<T> plan(shape);
        std::vector<float> pcm{.3f,-.2f,.5f,.7f,-.6f,0,.9f,.1f,-.8f};
        std::vector<C> oscillator;
        for(std::size_t i=0;i<pcm.size();++i)oscillator.push_back(std::polar(1.,.31+.73*i));
        std::vector<std::array<C,2>> rows{{C{.1,.4},C{-.3,.2}}};
        const tile::Input input{pcm,oscillator,{},500};const tile::Fragment fragment{rows,{},0};
        const std::array<std::size_t,3> starts{0,3,8};
        tile::PreparedInput<T> pi(plan,input,budget);tile::PreparedFragment<T> pf(plan,fragment,budget);
        tile::Accumulator<T> accumulator(plan,500,starts,budget);accumulator.add(pi,pf);
        const auto output=accumulator.finish();
        for(std::size_t at=0;at<starts.size();++at)for(unsigned bit=0;bit<2;++bit) {
            compare(output[at].fits[bit],original(input,fragment,starts[at],bit),tolerance,1e-6);
            CHECK(output[at].fits[bit].count==1 && output[at].requires_refinement[bit]);
        }
    }
    {
        const tile::Shape shape{17,128};tile::Plan<T> plan(shape);
        auto f=fixture(shape,128,4000,0,1500.,113);
        constexpr std::uint64_t chip=34,total=31*chip+13;
        std::array<std::array<C,2>,32> coefficients{};
        for(std::size_t j=0;j<coefficients.size();++j)
            coefficients[j]={std::polar(.15+.03*(j%5),.711*j),std::polar(.2+.02*(j%7),-.419*j)};
        for(std::size_t i=0;i<f.rows.size();++i) {
            const auto at=total-129+.137+i;
            for(unsigned bit=0;bit<2;++bit)f.rows[i][bit]=pattern_pulse_sum(at,total,chip,
                [&](std::uint64_t index){CHECK(index<coefficients.size());return coefficients[index][bit];});
            f.mask[i]=1;
        }
        const std::array<std::size_t,3> starts{0,7,16};
        tile::PreparedInput<T> input(plan,f.input(),budget);
        tile::PreparedFragment<T> fragment(plan,f.fragment(),budget);
        tile::Accumulator<T> accumulator(plan,f.origin,starts,budget);accumulator.add(input,fragment);
        const auto output=accumulator.finish();
        for(std::size_t i=0;i<starts.size();++i)for(unsigned bit=0;bit<2;++bit)
            compare(output[i].fits[bit],original(f.input(),f.fragment(),starts[i],bit),tolerance,1e-5);
    }
}

template<class T>void reuse_and_empty() {
    const tile::Shape shape{73,64};tile::Plan<T> plan(shape);
    const std::array<std::size_t,3> starts{0,17,72};auto f=fixture(shape,64,99999,0,1499.9,7);
    tile::PreparedInput<T> input(plan,f.input(),budget);
    tile::PreparedFragment<T> fragment(plan,f.fragment(),budget);
    tile::Accumulator<T> first(plan,99999,starts,budget),second(plan,99999,starts,budget);
    first.add(input,fragment);second.add(input,fragment);
    const auto a=first.finish(),b=second.finish();
    for(std::size_t i=0;i<starts.size();++i)for(unsigned bit=0;bit<2;++bit)
        compare(a[i].fits[bit],b[i].fits[bit],0,0);
    tile::Accumulator<T> empty(plan,99999,{},tile::helper_reservation+sizeof(tile::Accumulator<T>));
    empty.add(input,fragment);CHECK(empty.finish().empty());CHECK(empty.work().inverse_transforms==0);
}

template<class T>void dense_tile_and_long_reduction() {
    constexpr double tolerance=std::is_same_v<T,float>?8e-5:3e-10;
    {
        // Valid overlap-save outputs include the very last admitted start.
        // S+D==8192 fits one 8192 FFT, not the larger full linear-convolution FFT.
        const tile::Shape shape{6145,2048};tile::Plan<T> plan(shape);CHECK(plan.size==8192);
        const std::array<std::size_t,7> starts{0,1,17,1024,4096,6143,6144};
        auto f=fixture(shape,2048,123456789,0,1500.,721,true);
        tile::PreparedInput<T> input(plan,f.input(),budget);
        tile::PreparedFragment<T> fragment(plan,f.fragment(),budget);
        tile::Accumulator<T> accumulator(plan,f.origin,starts,budget);accumulator.add(input,fragment);
        const auto output=accumulator.finish();
        for(std::size_t i=0;i<starts.size();++i)for(unsigned bit=0;bit<2;++bit)
            compare(output[i].fits[bit],original(f.input(),f.fragment(),starts[i],bit),tolerance,1e-4);
    }
    {
        const tile::Shape shape{257,34};tile::Plan<T> plan(shape);
        const std::array<std::size_t,4> starts{0,17,128,256};
        constexpr std::uint64_t origin=18000000000000ULL;
        tile::Accumulator<T> accumulator(plan,origin,starts,budget);
        std::vector<tile::Value> direct(starts.size());
        for(unsigned k=0;k<64;++k) {
            auto f=fixture(shape,34-k%7,origin,k*1000000ULL,.005,137+k,k%11==0);
            tile::PreparedInput<T> input(plan,f.input(),budget);
            tile::PreparedFragment<T> fragment(plan,f.fragment(),budget);
            accumulator.add(input,fragment);tile::direct_add(f.input(),f.fragment(),origin,starts,direct);
        }
        const auto output=accumulator.finish();CHECK(accumulator.work().inverse_transforms==6);
        CHECK(accumulator.work().fragments==64);
        for(std::size_t i=0;i<starts.size();++i)for(unsigned bit=0;bit<2;++bit)
            compare(output[i].fits[bit],direct[i].fits[bit],tolerance,1e-4);
    }
}

void validation_and_recovery() {
    const tile::Shape shape{17,34};tile::Plan<double> plan(shape);
    const std::array<std::size_t,3> starts{0,8,16};auto f=fixture(shape,31,100,0,.5,123);
    tile::PreparedInput<double> input(plan,f.input(),budget);
    tile::PreparedFragment<double> fragment(plan,f.fragment(),budget);
    rejects([&]{tile::Accumulator<double> too_small(plan,100,starts,1);});
    rejects([&]{tile::PreparedInput<double> too_small(plan,f.input(),1);});
    rejects([&]{tile::PreparedFragment<double> too_small(plan,f.fragment(),1);});
    const std::array<std::size_t,2> duplicates{1,1};
    rejects([&]{tile::Accumulator<double> invalid(plan,100,duplicates,budget);});
    tile::Accumulator<double> overlap(plan,100,starts,budget);overlap.add(input,fragment);
    rejects([&]{overlap.add(input,fragment);});rejects([&]{overlap.finish();});
    tile::Accumulator<double> wrong_origin(plan,101,starts,budget);
    rejects([&]{wrong_origin.add(input,fragment);});
    std::stop_source cancelled;cancelled.request_stop();
    rejects([&]{tile::Accumulator<double> stopped(plan,100,starts,budget,cancelled.get_token());});
    tile::Accumulator<double> stopped(plan,100,starts,budget);
    rejects([&]{stopped.add(input,fragment,cancelled.get_token());});rejects([&]{stopped.finish();});
    rejects([&]{tile::Accumulator<double> invalid(plan,std::numeric_limits<std::uint64_t>::max()-3,starts,budget);});
    auto bad=f;
    bad.mask[0]=2;rejects([&]{tile::PreparedFragment<double> invalid(plan,bad.fragment(),budget);});bad.mask[0]=0;
    bad.pcm[2]=std::numeric_limits<float>::infinity();
    rejects([&]{tile::PreparedInput<double> invalid(plan,bad.input(),budget);});
    bad.pcm[2]=1;bad.oscillator[2]={std::numeric_limits<double>::quiet_NaN(),0};
    rejects([&]{tile::PreparedInput<double> invalid(plan,bad.input(),budget);});
    tile::Plan<float> fp(shape);bad=f;
    for(auto& x:bad.pcm)x=1e20f;
    rejects([&]{tile::PreparedInput<float> overflow(fp,bad.input(),budget);});
    for(auto& x:bad.pcm)x=1e-30f;
    rejects([&]{tile::PreparedInput<float> tiny(fp,bad.input(),budget);});
    std::vector<tile::Value> values(starts.size());
    rejects([&]{tile::direct_add(f.input(),f.fragment(),100,starts,values,nullptr,cancelled.get_token());});
}

void singular_and_energy_subtraction() {
    const tile::Shape shape{17,2};tile::Plan<double> plan(shape);
    std::vector<float> pcm(18,1e-8f);pcm[0]=1e12f;
    std::vector<C> oscillator(18,{1,0});std::vector<std::array<C,2>> rows(2,{C{1,0},C{2,0}});
    const std::array<std::size_t,1> starts{16};
    tile::PreparedInput<double> input(plan,{pcm,oscillator,{},100},budget);
    tile::PreparedFragment<double> fragment(plan,{rows,{},0},budget);
    tile::Accumulator<double> accumulator(plan,100,starts,budget);accumulator.add(input,fragment);
    const auto result=accumulator.finish();CHECK(accumulator.work().energy_refinements==1);
    CHECK(result[0].requires_refinement[0]);CHECK(result[0].requires_refinement[1]);
    CHECK(near(result[0].fits[0].energy,2*static_cast<double>(pcm[16])*pcm[16],1e-12,1e-30));
    const std::vector<std::uint8_t> missing(18,0);
    tile::PreparedInput<double> empty(plan,{pcm,oscillator,missing,100},budget);
    tile::Accumulator<double> no_observations(plan,100,starts,budget);no_observations.add(empty,fragment);
    const auto output=no_observations.finish();CHECK(output[0].fits[0].count==0);
    CHECK(output[0].fits[0].energy==0 && output[0].fits[0].cc==0 && output[0].fits[0].xc==0);

    // Operand quantization can give a rank-one Gram a tiny positive determinant.
    // FP32 must not treat that as a qualified, well-conditioned complex basis.
    const tile::Shape rank_shape{1,8};tile::Plan<float> fp(rank_shape);
    std::vector<float> x(8,.2f);std::vector<C> o(8,{1,0});
    std::vector<std::array<C,2>> rank_rows(8,{C{.01,.25},C{.17,-.31}});
    const std::array<std::size_t,1> rank_start{0};
    tile::PreparedInput<float> ri(fp,{x,o,{},1},budget);
    tile::PreparedFragment<float> rf(fp,{rank_rows,{},0},budget);
    tile::Accumulator<float> ra(fp,1,rank_start,budget);ra.add(ri,rf);
    const auto rank_result=ra.finish();
    CHECK(rank_result[0].requires_refinement[0] && rank_result[0].requires_refinement[1]);
}
int main() {
    hand_oracles();paired_fragments<float>();paired_fragments<double>();
    reuse_and_empty<float>();reuse_and_empty<double>();validation_and_recovery();singular_and_energy_subtraction();
    dense_tile_and_long_reduction<float>();dense_tile_and_long_reduction<double>();
    one_sample_and_partial_chip<float>();one_sample_and_partial_chip<double>();
    std::cout<<"scheduled whole-Fit spectral accumulation fixtures passed; no receiver/sensitivity qualification\n";
}
