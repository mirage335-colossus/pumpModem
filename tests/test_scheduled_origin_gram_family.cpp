#include "../src/scheduled_origin_gram.hpp"
#include "../src/scheduled_frequency_moments.hpp"
#include "../src/scheduled_lazy_family_runtime.hpp"
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace d=datapump::modem::detail;namespace g=d::scheduled_origin_gram;
namespace t=d::scheduled_acquisition_tile;namespace f=d::scheduled_frequency_moments;
using Complex=std::complex<double>;
#define CHECK(...) do{if(!(__VA_ARGS__))throw std::runtime_error(#__VA_ARGS__);}while(false)
constexpr std::size_t budget=64*1024*1024,N=511,span=17,degree=3;
constexpr std::uint32_t sample_rate=1024;constexpr double tau=2*std::numbers::pi;
std::vector<Complex> carrier(double hz){std::vector<Complex> result(N+span-1);const auto step=std::polar(1.,tau*hz/sample_rate);
    for(std::size_t first=0;first<result.size();first+=512){auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(first)*tau*hz/sample_rate,static_cast<long double>(tau))));
        for(auto i=first;i<std::min(result.size(),first+512);++i){result[i]=o;o*=step;}}return result;}
template<class T>void run(){
    const std::array<std::size_t,3> starts{0,7,16};t::Plan<T> plan({span,N});
    std::vector<float> pcm(N+span-1);std::vector<std::uint8_t> observed(pcm.size());
    constexpr std::size_t capture_first=11,capture_end=N+span-14;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=float(.17*std::sin(.19*i)+.06*std::cos(.017*i));observed[i]=i>=capture_first&&i<capture_end;}
    const auto coarse=carrier(31);const t::Input base{pcm,coarse,observed,0};f::MomentInput<T> moment(plan,base,degree,budget);
    CHECK(moment.work.shared_forward_transforms==3+2*degree); // honest complete shared representation
    std::vector<std::array<Complex,2>> rows(N);std::vector<g::Prefix<1>::Row> packed(N);std::vector<std::uint8_t> mask(N);
    std::size_t source=0;for(std::size_t i=0;i<N;++i){mask[i]=i%240<72;if(mask[i]){
        rows[i]={std::polar(.4+.2*std::sin(.13*source),.23*source),std::polar(.3+.17*std::cos(.17*source),.51*source)};
        if(i%59==0)rows[i]={};++source;}packed[i][0]=rows[i];}
    std::vector<std::uint64_t> points{0,N};std::array<std::pair<std::uint64_t,std::uint64_t>,3> crops;
    for(std::size_t j=0;j<starts.size();++j){crops[j]={capture_first>starts[j]?capture_first-starts[j]:0,
        std::min(N,capture_end-starts[j])};points.push_back(crops[j].first);points.push_back(crops[j].second);}
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    t::PreparedFragment<T> full_template(plan,{rows,mask,0},budget);
    t::PreparedFragment<T> reduced_template(plan,{rows,mask,0},budget,{},g::Mode::external_gram);
    for(unsigned target=0;target<4;++target){const auto delta=.0007*target,hz=31+delta;const auto other=carrier(hz);
        const std::span<const Complex> oscillator=target?std::span<const Complex>(other):std::span<const Complex>(coarse);
        const t::Input actual{pcm,oscillator,observed,0};const auto coefficients=f::coefficients(moment,delta,sample_rate,0);
        const auto remainder=f::sampled_remainder(moment,coefficients,actual);CHECK(remainder.valid);
        f::ResolvedInput<T> full(moment,coefficients,actual,remainder,budget);
        f::ResolvedInput<T> reduced(moment,coefficients,actual,remainder,budget,{},g::Mode::external_gram);
        CHECK(full.mode==g::Mode::complete&&reduced.mode==g::Mode::external_gram);
        CHECK(reduced.spectra[1].empty()&&reduced.spectra[2].empty());
        CHECK(full.spectra[0].size()==reduced.spectra[0].size());
        for(std::size_t i=0;i<plan.size;++i)CHECK(full.spectra[0][i]==reduced.spectra[0][i]);
        CHECK(full.certificates[0].forward_radius==reduced.certificates[0].forward_radius);
        if(target){CHECK(reduced.work.synthesized_products==(degree+1)*plan.size);
            CHECK(full.work.synthesized_products==2*reduced.work.synthesized_products);
            CHECK(reduced.working_bytes()<full.working_bytes());}
        else CHECK(full.work.synthesized_products==0&&reduced.work.synthesized_products==0);
        g::Powers powers(std::polar(1.,tau*hz/sample_rate));g::RawReference reference(powers,target+7);
        for(std::size_t first=0;first<pcm.size();first+=512)CHECK(reference.append(first,std::min<std::size_t>(512,pcm.size()-first),oscillator[first]));
        g::Prefix<1> prefix(powers,target+101,points,budget);prefix.append(0,packed,mask);prefix.finish(N);
        std::vector<std::array<g::Gram,2>> grams;for(std::size_t j=0;j<starts.size();++j)
            grams.push_back(prefix.query(crops[j].first,crops[j].second,starts[j],reference,true)[0]);
        const g::Binding binding{target+101,target+7,reference.revision()};
        const g::Override token{grams,starts,0,target+101,target+7,reference.revision()};
        t::Accumulator<T> baseline(plan,0,starts,budget);baseline.add(full,full_template);const auto normal=baseline.finish();
        t::Accumulator<T> optimized(plan,0,starts,budget,{},false,false,g::Mode::external_gram);
        optimized.add(reduced,reduced_template);CHECK(optimized.accepts_origin_gram(token,binding));
        const auto values=optimized.finish({},nullptr,&token,&binding);CHECK(optimized.work().inverse_transforms==2);
        for(std::size_t j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit){const auto& a=normal[j].fits[bit];const auto& b=values[j].fits[bit];
            CHECK(a.xc==b.xc&&a.xs==b.xs&&a.count==b.count&&a.energy==b.energy&&b.count>0);
            const auto allowance=normal[j].certificate.gram_radius[bit]+values[j].certificate.gram_radius[bit];
            CHECK(std::abs(a.cc-b.cc)<=allowance&&std::abs(a.ss-b.ss)<=allowance&&std::abs(a.cs-b.cs)<=allowance);
        }
        CHECK(d::scheduled_lazy_family_runtime::input_bytes(plan,g::Mode::external_gram)<d::scheduled_lazy_family_runtime::input_bytes(plan));
        CHECK(d::scheduled_lazy_family_runtime::resolved_bytes(plan,g::Mode::external_gram)<d::scheduled_lazy_family_runtime::resolved_bytes(plan));
        CHECK(d::scheduled_lazy_family_runtime::accumulator_bytes(plan,starts.size(),false,g::Mode::external_gram)<d::scheduled_lazy_family_runtime::accumulator_bytes(plan,starts.size()));
    }
}
int main(){try{run<float>();run<double>();std::cout<<"origin-Gram shared moment/family mode tests PASS\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
