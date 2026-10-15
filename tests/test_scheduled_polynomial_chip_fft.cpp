#include "scheduled_polynomial_chip_fft.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace d=datapump::modem::detail;
namespace p=d::scheduled_polynomial_chip;
namespace f=d::scheduled_polynomial_chip_fft;
namespace c=d::scheduled_acquisition_certificate;
using Complex=std::complex<double>;using Wide=std::complex<long double>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F run,const char* message) {
    bool caught=false;try{run();}catch(const datapump::Error&){caught=true;}check(caught,message);
}
void encloses(c::Interval interval,long double exact,const char* message) {
    const auto allowance=512*std::numeric_limits<long double>::epsilon()*std::max(1.L,std::abs(exact));
    check(interval.bounded() && interval.lo-allowance<=exact && exact<=interval.hi+allowance,message);
}
struct Fixture {
    std::vector<float> pcm;std::vector<Complex> oscillator;std::vector<std::uint8_t> observed;
    p::PhysicalKey key{71,73,79,83,37,89,10000000};
    explicit Fixture(std::size_t count):pcm(count),oscillator(count),observed(count,1) {
        const auto step=std::polar(1.,.247);Complex o{};
        for(std::size_t i=0;i<count;++i) {
            // Preserve actual finite independent reset identities, not an
            // analytical rotation or forced unit magnitude approximation.
            if(i%512==0)o=std::polar(.93+.0001*(i/512),.371*(i+key.physical_first));
            oscillator[i]=o;o*=step;
            pcm[i]=static_cast<float>(.3*std::sin(.177*i)+.19*std::cos(.071*i));
        }
    }
    f::InputTile input()const{return {key,pcm,oscillator,observed};}
};
void all_planes(const Fixture& fixture,std::size_t first,const p::Moments& moments) {
    p::Basis basis;
    std::array<Wide,4> dot{};std::array<Wide,7> image{};std::array<long double,7> trace{};
    long double energy=0,carrier_max=0;
    for(std::size_t n=0;n<34;++n) {
        const auto input=fixture.oscillator[first+n];const Wide o{input.real(),input.imag()};
        const auto x=static_cast<long double>(fixture.pcm[first+n]),t=static_cast<long double>(basis.t[n]);
        long double power=1;
        for(std::size_t k=0;k<7;++k) {
            if(k<4)dot[k]+=x*o*power;image[k]+=o*o*power;trace[k]+=std::norm(o)*power;power*=t;
        }
        energy+=x*x;carrier_max=std::max(carrier_max,std::norm(o));
    }
    check(p::valid_moments(moments) && moments.count==34,"FFT producer returned incomplete moments");
    for(std::size_t k=0;k<4;++k){encloses(moments.dot[k].real,dot[k].real(),"D real escaped FFT certificate");
        encloses(moments.dot[k].imag,dot[k].imag(),"D imag escaped FFT certificate");}
    for(std::size_t k=0;k<7;++k){encloses(moments.image[k].real,image[k].real(),"K real escaped FFT certificate");
        encloses(moments.image[k].imag,image[k].imag(),"K imag escaped FFT certificate");
        encloses(moments.trace[k].interval,trace[k],"T escaped FFT certificate");}
    encloses(moments.energy.interval,energy,"positive prefix energy escaped certificate");
    check(moments.carrier_square_upper>=carrier_max,"tile max carrier bound was too small");
}
void endpoints_and_all_eighteen_planes() {
    Fixture input(256);const f::Plan plan({256,4*1024*1024});
    const std::array<std::size_t,6> selected{0,1,17,33,117,222};
    const auto result=f::execute(input.input(),selected,plan);
    check(result.complete && result.entries.size()==selected.size(),"missing complete selected windows");
    check(plan.kernel_transforms()==7 && result.work.input_transforms==3 && result.work.inverse_transforms==18 &&
        result.work.spectral_products==18*256,"not all polynomial planes executed once");
    check(plan.working_bytes()<=f::Plan::reservation({256,4*1024*1024}),"plan constructor reservation too small");
    check(result.work.execution_bytes<=plan.execution_reservation(input.pcm.size(),selected.size()),"execution reservation too small");
    for(std::size_t j=0;j<selected.size();++j) {
        check(result.entries[j].key.physical_first==input.key.physical_first+selected[j] &&
            result.entries[j].key.fence==input.key.fence,"physical window key changed");
        all_planes(input,selected[j],result.entries[j].moments);
    }
}
void source_contract_and_cache_injection() {
    Fixture input(256);const f::Plan plan({256,4*1024*1024});
    const std::array<std::size_t,4> selected{0,17,187,222};
    std::array<std::array<Complex,2>,34> q{};p::Basis basis;
    for(std::size_t n=0;n<34;++n){const auto t=basis.t[n];
        q[n][0]=Complex{.1,.2}+t*(Complex{.2,-.1}+t*(Complex{-.07,.01}+t*Complex{.03,.04}));
        q[n][1]=Complex{-.2,.1}+t*Complex{.04,-.03};}
    q[11][0]+=Complex{.003,-.002}; // nonzero source residual must remain covered
    const auto model=p::build_source(q);
    const auto result=f::execute(input.input(),selected,plan);
    const std::array<c::ReferenceErrors,2> original{{{0,0,0,c::infinity,true},{0,0,0,c::infinity,true}}};
    for(std::size_t j=0;j<selected.size();++j) {
        const auto fields=p::contract(model,result.entries[j].moments,original);check(fields.supported,"FFT Moments incompatible with source contract");
        for(unsigned bit=0;bit<2;++bit) {
            d::CorrelationFit direct;
            for(std::size_t n=0;n<34;++n){const auto at=selected[j]+n;const auto x=static_cast<double>(input.pcm[at]);const auto o=input.oscillator[at];
                direct.add({x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x},q[n][bit],1);}
            const auto& bounded=fields.intervals[bit];
            encloses(bounded.xc,direct.xc,"Original chip xc escaped combined FFT/source interval");
            encloses(bounded.xs,direct.xs,"Original chip xs escaped combined FFT/source interval");
            encloses(bounded.cc,direct.cc,"Original chip cc escaped combined FFT/source interval");
            encloses(bounded.ss,direct.ss,"Original chip ss escaped combined FFT/source interval");
            encloses(bounded.cs,direct.cs,"Original chip cs escaped combined FFT/source interval");
            encloses(bounded.energy,direct.energy,"Original chip energy escaped combined FFT/source interval");
        }
    }
    p::MomentCache cache(8,64*1024);const auto work=f::fill_cache(input.input(),selected,plan,cache);
    check(work.cache_installs==selected.size() && cache.counters().certified_builds==selected.size() &&
        cache.counters().physical_sample_rows==0,"prepared FFT output counted as FIR physical row work");
    check(work.input_rows==256,"FFT tile input work omitted");
    for(const auto& entry:result.entries) {
        const auto* found=cache.find(entry.key);check(found && found->count==34,"certified cache entry not available before raw gather");
    }
    const auto again=f::fill_cache(input.input(),selected,plan,cache);
    check(again.cache_installs==0,"existing complete entries counted as new FFT publications");
    auto invalid=result.entries[0].moments;invalid.image[3].real={1,-1};
    auto newkey=result.entries[0].key;++newkey.physical_first;
    const auto builds=cache.counters().builds;
    rejects([&]{cache.insert_certified(newkey,invalid);},"invalid prepared certificate evicted cache state");
    check(cache.counters().builds==builds,"invalid prepared insertion mutated cache");
}
void modulo_seventeen_default_tile() {
    Fixture input(4096);const f::Plan plan;
    std::vector<std::size_t> selected;for(std::size_t start=16;start+34<=input.pcm.size();start+=17)selected.push_back(start);
    check(plan.maximum_payload_samples()==4063 && selected.size()==239 && selected.back()==4062,"default physical residue geometry changed");
    const auto result=f::execute(input.input(),selected,plan);
    check(result.work.input_transforms==3 && result.work.inverse_transforms==18,"selected windows caused individual transforms");
    for(const std::size_t at:{0U,119U,238U})all_planes(input,selected[at],result.entries[at].moments);
    check(result.work.workspace_bytes<=plan.config.workspace_bytes,"default tile exceeded combined budget");
}
void positive_prefix_and_covariance_with_zero_pcm() {
    Fixture input(128);const f::Plan plan({128,2*1024*1024});
    for(std::size_t i=0;i<input.pcm.size();++i)input.pcm[i]=i<64?1e8f:1e-4f;
    const std::array<std::size_t,1> selected{94};
    auto result=f::execute(input.input(),selected,plan);all_planes(input,94,result.entries[0].moments);
    for(auto& x:input.pcm)x=0;
    result=f::execute(input.input(),selected,plan);all_planes(input,94,result.entries[0].moments);
    check(result.entries[0].moments.count==34 && result.entries[0].moments.energy.value==0 &&
        result.entries[0].moments.trace[0].value>0,"zero PCM removed covariance or physical count");
    check(std::abs(result.entries[0].moments.image[1].value)>1e-4,"complex covariance image plane silently omitted");
}
void invalid_windows_missing_rows_cancellation_budget() {
    Fixture input(128);const f::Plan plan({128,2*1024*1024});const std::array<std::size_t,1> selected{17};
    auto result=f::execute(input.input(),selected,plan);const auto saved=result.entries[0].moments.dot[0].value;
    std::stop_source cancelled;cancelled.request_stop();
    rejects([&]{result=f::execute(input.input(),selected,plan,cancelled.get_token());},"FFT tile cancellation published result");
    check(result.entries[0].moments.dot[0].value==saved,"cancelled execute replaced caller output");
    const std::array<std::size_t,1> beyond{95};rejects([&]{result=f::execute(input.input(),beyond,plan);},"oversized physical window admitted");
    const std::array<std::size_t,2> duplicate{17,17};rejects([&]{result=f::execute(input.input(),duplicate,plan);},"duplicate physical windows admitted");
    input.observed[31]=0;rejects([&]{result=f::execute(input.input(),selected,plan);},"missing physical row manufactured a complete C34 cell");
    input.observed[31]=1;input.key.fence=0;rejects([&]{result=f::execute(input.input(),selected,plan);},"missing Original fence accepted");input.key.fence=79;
    rejects([&]{f::Plan short_plan({128,1});},"short Plan budget accepted");
    const auto persistent=f::Plan::reservation({128,2*1024*1024});const f::Plan short_execution({128,persistent+1024});
    rejects([&]{result=f::execute(input.input(),selected,short_execution);},"short execution budget accepted");
    p::MomentCache cache(1,64*1024);const std::array<std::size_t,2> two{0,17};
    rejects([&]{(void)f::fill_cache(input.input(),two,plan,cache);},"prefetch exceeded cache output capacity");
    check(cache.counters().builds==0,"failed FFT fill published a partial output");
    auto valid=result.entries[0].moments;
    rejects([&]{cache.insert_certified(result.entries[0].key,valid,cancelled.get_token());},"cancelled certified insert mutated cache");
    check(cache.counters().builds==0,"cancelled prepared insertion changed cache count");
}
int main(){try {
    endpoints_and_all_eighteen_planes();source_contract_and_cache_injection();modulo_seventeen_default_tile();
    positive_prefix_and_covariance_with_zero_pcm();invalid_windows_missing_rows_cancellation_budget();
    std::cout<<"polynomial FFT moments: five focused cases passed; N4096plan="<<f::Plan::reservation()
        <<" Moments="<<sizeof(p::Moments)<<" Entry="<<sizeof(f::Entry)<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
