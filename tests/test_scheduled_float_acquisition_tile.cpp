#include "scheduled_acquisition_tile.hpp"
#include "scheduled_frequency_moments.hpp"
#include "scheduled_lazy_family_runtime.hpp"
#include "scheduled_parallel_preparation.hpp"
#include "datapump/pattern_pulse.hpp"
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

namespace d=datapump::modem::detail;
namespace t=d::scheduled_acquisition_tile;
namespace c=d::scheduled_acquisition_certificate;
namespace r=d::scheduled_lazy_family_runtime;
using C=std::complex<double>;
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);} while(false)
template<class Fn>void rejects(Fn fn){bool rejected=false;try{fn();}catch(const datapump::Error&){rejected=true;}CHECK(rejected);}
constexpr std::size_t budget=32*1024*1024;
void merge(d::CorrelationFit& a,const d::CorrelationFit& b) {
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
}
void exact_memory() {
    const t::Shape shape{8192,2048};t::Plan<float> plan(shape);
    const std::array<std::size_t,3> starts{0,17,8191};
    t::Accumulator<float> wide(plan,100,starts,budget),native(plan,100,starts,budget,{},false,true);
    CHECK(plan.size==16384);CHECK(!wide.native_float_accumulation() && native.native_float_accumulation());
    CHECK(wide.working_bytes()-native.working_bytes()==12*plan.size*(sizeof(std::complex<double>)-sizeof(std::complex<float>)));
    CHECK(r::accumulator_bytes(plan,starts.size(),false)>=wide.working_bytes());
    CHECK(r::accumulator_bytes(plan,starts.size(),true)>=native.working_bytes());
    CHECK(r::accumulator_bytes(plan,starts.size(),false)-r::accumulator_bytes(plan,starts.size(),true)==wide.working_bytes()-native.working_bytes());
    const auto a=d::scheduled_parallel_preparation::scratch_bytes(8192,starts.size(),2048,false);
    const auto b=d::scheduled_parallel_preparation::scratch_bytes(8192,starts.size(),2048,true);
    CHECK(a-b==wide.working_bytes()-native.working_bytes());
    rejects([&]{t::Accumulator<float> small(plan,100,starts,native.working_bytes()-1,{},false,true);});
    t::Plan<double> forced(shape);
    rejects([&]{t::Accumulator<double> narrowed(forced,100,starts,budget,{},false,true);});
    rejects([&]{r::accumulator_bytes(forced,starts.size(),true);});
    const std::span<const std::size_t> empty;
    t::Accumulator<float> none(plan,100,empty,budget,{},false,true);
    CHECK(none.finish().empty());CHECK(none.work().inverse_transforms==0);
}
void complete_fragments() {
    const t::Shape shape{73,129};t::Plan<float> plan(shape);
    const std::array<std::size_t,5> starts{0,1,17,36,72};
    constexpr std::uint64_t origin=std::uint64_t{1}<<51;
    t::Accumulator<float> wide(plan,origin,starts,budget),native(plan,origin,starts,budget,{},true,true);
    t::Accumulator<float> native_same(plan,origin,starts,budget,{},false,true);
    std::vector<t::Value> truth(starts.size());
    std::mt19937 rng(778211);std::normal_distribution<double> normal;
    const auto samples=shape.start_span+shape.maximum_fragment-1;
    for(unsigned fragment=0;fragment<97;++fragment) {
        const std::uint64_t offset=fragment==0?0:(std::uint64_t{1}<<39)+fragment*701;
        std::vector<float> pcm(samples);std::vector<C> carrier(samples);
        std::vector<std::uint8_t> observed(samples,1),mask(129,1);
        std::vector<std::array<C,2>> rows(129);std::array<std::array<C,2>,64> coefficients;
        for(auto& pair:coefficients)for(auto& q:pair)q={normal(rng)*.4,normal(rng)*.4};
        for(std::size_t n=0;n<samples;++n) {
            auto x=normal(rng)*.17;if(fragment%11==0)x=std::clamp(x*9,-.99,.99);
            pcm[n]=static_cast<float>(x);observed[n]=n%37?1:0;
            const auto phase=std::remainder((static_cast<long double>(origin)+offset+n)*
                (2*std::numbers::pi_v<long double>)*1500/6120,2*std::numbers::pi_v<long double>);
            carrier[n]=std::polar(1+1e-12*n,static_cast<double>(phase));
        }
        for(std::size_t n=0;n<rows.size();++n) {
            for(unsigned bit=0;bit<2;++bit) {
                const auto shaped=datapump::modem::pattern_pulse_sum(17*17+.123+n*1.00001,64*17-3,17,
                    [&](std::uint64_t k){return coefficients[k][bit];});
                const auto gain=.1+.9*std::pow(std::sin(std::numbers::pi*(n+.5)/rows.size()),2);
                rows[n][bit]=gain*datapump::modem::pattern_limit_pcm(.2766992952647332*shaped)/.2766992952647332;
            }
            if(n%29==0)mask[n]=0;
            if(n%41==0)rows[n]={C{},C{}};
        }
        const t::Input original{pcm,carrier,observed,origin+offset};const t::Fragment templ{rows,mask,offset};
        t::PreparedInput<float> input(plan,original,budget);t::PreparedFragment<float> p(plan,templ,budget);
        wide.add(input,p);native.add(input,p);native_same.add(input,p);
        t::direct_add(original,templ,origin,starts,truth);
    }
    const auto a=wide.finish(),b=native.finish();const auto shared=native.trace_reuse();
    const auto with_reuse=native_same.finish({},&shared);
    CHECK(native.work().fragments==97);CHECK(native_same.work().trace_inverse_reuses==2);
    CHECK(native_same.work().inverse_transforms==4);
    for(std::size_t j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit) {
        const auto& actual=b[j].fits[bit];const auto& ref=truth[j].fits[bit];
        CHECK(actual.count==ref.count && actual.count==a[j].fits[bit].count);
        CHECK(actual.energy==a[j].fits[bit].energy);CHECK(b[j].certificate.valid);
        const auto original=c::scalar_reference_errors(ref.count,b[j].certificate.energy_upper,
            ref.cc+ref.ss+2*b[j].certificate.gram_radius[bit],0);
        CHECK(original.valid);
        const auto dot=c::add(b[j].certificate.dot_radius[bit],original.dot);
        const auto gram=c::add(b[j].certificate.gram_radius[bit],original.gram);
        CHECK(std::abs(actual.xc-ref.xc)<=dot);CHECK(std::abs(actual.xs-ref.xs)<=dot);
        CHECK(std::abs(actual.cc-ref.cc)<=gram);CHECK(std::abs(actual.ss-ref.ss)<=gram);CHECK(std::abs(actual.cs-ref.cs)<=gram);
        CHECK(actual.xc==with_reuse[j].fits[bit].xc && actual.xs==with_reuse[j].fits[bit].xs);
        CHECK(actual.cc==with_reuse[j].fits[bit].cc && actual.ss==with_reuse[j].fits[bit].ss && actual.cs==with_reuse[j].fits[bit].cs);
    }
    // Different arithmetic state cannot borrow a trace merely because geometry
    // and sampled PCM match. Constructor/failed state must also stay bounded.
    t::Accumulator<float> mismatched(plan,origin,starts,budget);
    CHECK(!mismatched.same_trace(shared));
    std::stop_source stop;stop.request_stop();t::Accumulator<float> poisoned(plan,origin,starts,budget,{},false,true);
    std::vector<float> pcm(samples);std::vector<C> carrier(samples,C{1,0});std::vector<std::array<C,2>> rows(129);
    t::PreparedInput<float> input(plan,{pcm,carrier,{},origin},budget);t::PreparedFragment<float> p(plan,{rows,{},0},budget);
    rejects([&]{poisoned.add(input,p,stop.get_token());});rejects([&]{poisoned.finish();});
    rejects([&]{native.add(input,p);});rejects([&]{native.finish();});
}
int main()try {exact_memory();complete_fragments();std::cout<<"native FP32 acquisition: memory, original-field certificate, exact trace and failure checks passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
