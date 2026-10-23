#include "resource_test_margin.hpp"
#include "../src/scheduled_origin_gram.hpp"
#include <cfenv>
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace d=datapump::modem::detail;namespace g=d::scheduled_origin_gram;
namespace c=d::scheduled_acquisition_certificate;namespace t=d::scheduled_acquisition_tile;
using Complex=std::complex<double>;
#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
constexpr std::size_t N=16367,span=33,hop=2448,budget=128*1024*1024;
struct Data {
    std::vector<float> pcm=std::vector<float>(N+span-1);
    std::vector<Complex> oscillator=std::vector<Complex>(pcm.size());
    std::vector<g::Prefix<3>::Row> rows=std::vector<g::Prefix<3>::Row>(N);
    std::vector<std::uint8_t> mask=std::vector<std::uint8_t>(N);
    const Complex step=std::polar(1.+2e-15,.31);g::Powers powers{step};g::RawReference reference{powers,17};
    explicit Data(bool omit_fence=false){
        std::size_t source=0;
        for(std::size_t first=0;first<pcm.size();first+=512){const auto count=std::min<std::size_t>(512,pcm.size()-first);
            const auto seed=std::polar(1.,.31*first+2e-8*std::sin(double(first/512)));
            if(!omit_fence||first!=1024)CHECK(reference.append(first,count,seed));
            auto o=seed;for(std::size_t k=0;k<count;++k){oscillator[first+k]=o;o*=step;}}
        for(std::size_t i=0;i<pcm.size();++i){pcm[i]=float(.3*std::sin(.373*i)+.13*std::cos(.017*i));if(i>=N)continue;
            mask[i]=(i/hop)%3==0;if(!mask[i])continue;const auto edge=i%hop;
            const double gain=edge<24?std::pow(std::sin(std::numbers::pi*(edge+.5)/48),2):
                edge>=hop-24?std::pow(std::sin(std::numbers::pi*(hop-edge-.5)/48),2):1.;
            for(unsigned bit=0;bit<2;++bit){const auto a=gain*std::polar(.3+.17*std::abs(std::sin((bit?.271:.143)*source)),(bit?.831:.577)*source);
                const auto b=a+gain*std::polar(1e-4,.13*source);
                rows[i][0][bit]=a;rows[i][1][bit]=b;rows[i][2][bit]=b-a;}
            if(i%113==0)rows[i]={};++source;}
        CHECK(reference.valid());CHECK(reference.radius()>0);
    }
    std::array<d::CorrelationFit,2> direct(std::size_t variant,std::size_t origin,std::size_t first,std::size_t end)const{
        std::array<d::CorrelationFit,2> out{};
        for(auto i=first;i<end;++i){if(!mask[i])continue;const auto at=origin+i;const double x=pcm[at];const auto o=oscillator[at];
            for(unsigned bit=0;bit<2;++bit)out[bit].add({x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x},rows[i][variant][bit],1);}
        return out;
    }
};
void powers_and_carrier(){Data data;for(const auto n:std::array<std::int64_t,7>{-400,-1,0,1,17,512,16367}){
    const auto positive=data.powers.at(n),negative=data.powers.at(-n);CHECK(positive.valid&&negative.valid);
    CHECK(g::separation({1,0},g::product(positive,negative))<1e-7);}
    CHECK(!data.powers.at(g::Powers::maximum_exponent+1).valid);
    CHECK(data.reference.covers(0,data.pcm.size()));
    g::RawReference duplicate(data.powers,9);CHECK(duplicate.append(0,512,{1,0}));const auto revision=duplicate.revision();
    CHECK(duplicate.append(0,512,{1,0})&&duplicate.revision()==revision);CHECK(!duplicate.append(0,511,{1,0}));
    Data gap(true);CHECK(!gap.reference.covers(1000,1600));
    const auto old=std::fegetround();CHECK(std::fesetround(FE_DOWNWARD)==0);CHECK(!data.powers.at(7).valid);
    CHECK(std::fesetround(old)==0);
}
void prefix_and_crops(){Data data;const std::array<std::uint64_t,8> points{0,83,491,2448,7344,8000,9792,N};
    g::Prefix<3> prefix(data.powers,0x1001,points,budget);for(std::size_t at=0;at<N;){const auto count=std::min<std::size_t>(777,N-at);
        prefix.append(at,std::span<const g::Prefix<3>::Row>(data.rows).subspan(at,count),std::span<const std::uint8_t>(data.mask).subspan(at,count));at+=count;}prefix.finish(N);CHECK(prefix.valid());CHECK(datapump::testing::benchmark_below(prefix.working_bytes(),std::size_t{2*1024*1024},"test_scheduled_origin_gram.prefix_working_bytes","workspace_bytes"));
    for(const auto origin:std::array<std::size_t,3>{0,7,32})for(const auto cut:std::array<std::pair<std::size_t,std::size_t>,4>{{{0,N},{83,9792},{491,N},{2448,7344}}}){
        const auto rounding=c::scalar_reference_errors(N,0,16*N,0).gram;
        const auto result=prefix.query(cut.first,cut.second,origin,data.reference,true,0,rounding);
        for(std::size_t variant=0;variant<3;++variant){const auto exact=data.direct(variant,origin,cut.first,cut.second);
            for(unsigned bit=0;bit<2;++bit){const auto& q=result[variant][bit];CHECK(q.supported);CHECK(q.count==exact[bit].count);CHECK(exact[bit].count==0 || exact[bit].energy>0);
                CHECK(std::abs(q.cc-exact[bit].cc)<=q.radius);CHECK(std::abs(q.ss-exact[bit].ss)<=q.radius);CHECK(std::abs(q.cs-exact[bit].cs)<=q.radius);
                CHECK(q.radius<.01);CHECK(q.template_identity==0x1001&&q.capture_generation==17&&q.capture_revision==data.reference.revision());}}
    }
    g::Prefix<3> components(data.powers,0x1001,points,budget);
    std::array<std::array<Complex,2>,71> a,b,difference;
    for(std::size_t at=0;at<N;at+=a.size()) {
        const auto count=std::min(a.size(),N-at);
        for(std::size_t j=0;j<count;++j){a[j]=data.rows[at+j][0];b[j]=data.rows[at+j][1];difference[j]=data.rows[at+j][2];}
        g::append_three(components,at,std::span(a).first(count),std::span(b).first(count),std::span(difference).first(count),
            std::span<const std::uint8_t>(data.mask).subspan(at,count));
    }components.finish(N);
    const auto packed=components.query(0,N,7,data.reference,true),ordinary=prefix.query(0,N,7,data.reference,true);
    for(std::size_t variant=0;variant<3;++variant)for(unsigned bit=0;bit<2;++bit){
        CHECK(packed[variant][bit].supported&&packed[variant][bit].count==ordinary[variant][bit].count);
        CHECK(std::abs(packed[variant][bit].cc-ordinary[variant][bit].cc)<=packed[variant][bit].radius+ordinary[variant][bit].radius);
    }
    CHECK(!prefix.query(0,N,0,data.reference,false)[0][0].supported);
    CHECK(!prefix.query(1,N,0,data.reference,true)[0][0].supported);
    Data gap(true);CHECK(!prefix.query(0,N,0,gap.reference,true)[0][0].supported);
    std::stop_source stop;stop.request_stop();g::Prefix<3> cancelled(data.powers,1,points,budget);bool threw=false;
    try{cancelled.append(0,data.rows,data.mask,stop.get_token());}catch(const std::exception&){threw=true;}CHECK(threw&&!cancelled.valid());
    bool huge=false;g::Prefix<3> oversized(data.powers,1,points,budget);
    try{oversized.append(~std::uint64_t{},std::span<const g::Prefix<3>::Row>(data.rows).first(1));}catch(const std::exception&){huge=true;}CHECK(huge);
    g::Prefix<1> zero(data.powers,2,std::array<std::uint64_t,2>{0,128},budget);std::vector<g::Prefix<1>::Row> blank(128);
    zero.append(0,blank);zero.finish(128);const auto z=zero.query(0,128,0,data.reference,true);
    CHECK(z[0][0].supported&&z[0][0].count==128&&z[0][0].cc==0&&z[0][0].ss==0&&z[0][0].cs==0); // no noise-degree omission
}
template<class T>void actual_accumulator_hook(){Data data;const std::array<std::size_t,3> starts{0,7,32};const t::Shape shape{span,N};
    const std::array<std::uint64_t,2> points{0,N};g::Prefix<3> prefix(data.powers,0x51c341,points,budget);prefix.append(0,data.rows,data.mask);prefix.finish(N);
    t::Plan<T> plan(shape);t::PreparedInput<T> input(plan,{data.pcm,data.oscillator,{},0},budget);
    for(std::size_t variant=0;variant<3;++variant){std::vector<std::array<Complex,2>> rows(N);for(std::size_t i=0;i<N;++i)rows[i]=data.rows[i][variant];
        t::PreparedFragment<T> fragment(plan,{rows,data.mask,0},budget);
        t::Accumulator<T> baseline(plan,0,starts,budget);baseline.add(input,fragment);const auto original=baseline.finish();
        std::vector<std::array<g::Gram,2>> grams;for(auto origin:starts)grams.push_back(prefix.query(0,N,origin,data.reference,true)[variant]);
        const g::Override token{grams,starts,0,0x51c341,17,data.reference.revision(),variant};
        const g::Binding binding{0x51c341,17,data.reference.revision(),variant};CHECK(token.valid_for(0,starts,binding));
        t::Accumulator<T> optimized(plan,0,starts,budget);optimized.add(input,fragment);const auto result=optimized.finish({},nullptr,&token,&binding);
        CHECK(optimized.work().origin_gram_inverse_reuses==4);CHECK(optimized.work().inverse_transforms==2);
        CHECK(baseline.work().inverse_transforms==6);
        for(std::size_t j=0;j<starts.size();++j){const auto exact=data.direct(variant,starts[j],0,N);
            for(unsigned bit=0;bit<2;++bit){const auto& fit=result[j].fits[bit];const auto& ref=original[j].fits[bit];
                CHECK(fit.count==ref.count&&fit.count==exact[bit].count);CHECK(fit.energy==ref.energy);
                CHECK(fit.xc==ref.xc&&fit.xs==ref.xs);CHECK(result[j].certificate.valid);
                const auto round=c::scalar_reference_errors(N,0,16*N,0).gram;
                CHECK(std::abs(fit.cc-exact[bit].cc)<=result[j].certificate.gram_radius[bit]+round);
                CHECK(std::abs(fit.ss-exact[bit].ss)<=result[j].certificate.gram_radius[bit]+round);
                CHECK(std::abs(fit.cs-exact[bit].cs)<=result[j].certificate.gram_radius[bit]+round);}}
        const auto mode=g::Mode::external_gram;
        t::PreparedInput<T> reduced_input(plan,{data.pcm,data.oscillator,{},0},budget,{},mode);
        t::PreparedFragment<T> reduced_fragment(plan,{rows,data.mask,0},budget,{},mode);
        CHECK(reduced_input.work.input_transforms==1&&input.work.input_transforms==3);
        CHECK(reduced_fragment.work.template_transforms==2&&fragment.work.template_transforms==6);
        CHECK(reduced_input.working_bytes()<input.working_bytes()&&reduced_fragment.working_bytes()<fragment.working_bytes());
        const auto reduced_case=[&](bool native) {
            t::Accumulator<T> complete_native(plan,0,starts,budget,{},false,native);complete_native.add(input,fragment);
            const auto complete_values=complete_native.finish();
            t::Accumulator<T> reduced(plan,0,starts,budget,{},false,native,mode);reduced.add(reduced_input,reduced_fragment);
            const auto reduced_values=reduced.finish({},nullptr,&token,&binding);
            CHECK(reduced.work().inverse_transforms==2&&reduced.work().spectral_products==2*plan.size);
            CHECK(reduced.working_bytes()<complete_native.working_bytes());
            for(std::size_t j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit) {
                CHECK(reduced_values[j].fits[bit].xc==complete_values[j].fits[bit].xc);
                CHECK(reduced_values[j].fits[bit].xs==complete_values[j].fits[bit].xs);
                CHECK(reduced_values[j].fits[bit].count==complete_values[j].fits[bit].count);
                CHECK(reduced_values[j].fits[bit].energy==complete_values[j].fits[bit].energy);
                CHECK(reduced_values[j].certificate.valid);
            }
        };reduced_case(false);if constexpr(std::is_same_v<T,float>)reduced_case(true);
        bool rejected_missing=false;t::Accumulator<T> missing(plan,0,starts,budget,{},false,false,mode);
        missing.add(reduced_input,reduced_fragment);try{(void)missing.finish();}catch(const std::exception&){rejected_missing=true;}
        CHECK(rejected_missing); // mode cannot silently consume absent Gram spectra
        bool rejected_mixing=false;t::Accumulator<T> mixed(plan,0,starts,budget,{},false,false,mode);
        try{mixed.add(input,fragment);}catch(const std::exception&){rejected_mixing=true;}CHECK(rejected_mixing);
        auto swapped=token;swapped.variant=(variant+1)%3;CHECK(!swapped.valid_for(0,starts,binding));
        const auto invalid_trace=optimized.trace_reuse();t::Accumulator<T> ordinary_after_override(plan,0,starts,budget);
        ordinary_after_override.add(input,fragment);const auto independent=ordinary_after_override.finish({},&invalid_trace);
        CHECK(ordinary_after_override.work().trace_reuse_fallbacks==1&&ordinary_after_override.work().inverse_transforms==6);
        CHECK(independent[0].fits[0].cc==original[0].fits[0].cc);
        auto wrong_binding=binding;++wrong_binding.capture_revision;t::Accumulator<T> wrong_identity(plan,0,starts,budget);
        wrong_identity.add(input,fragment);(void)wrong_identity.finish({},nullptr,&token,&wrong_binding);
        CHECK(wrong_identity.work().origin_gram_fallbacks==1&&wrong_identity.work().inverse_transforms==6);
        ++grams[0][0].count;t::Accumulator<T> fallback(plan,0,starts,budget);fallback.add(input,fragment);CHECK(!fallback.accepts_origin_gram(token,binding));const auto unchanged=fallback.finish({},nullptr,&token,&binding);
        CHECK(fallback.work().origin_gram_fallbacks==1&&fallback.work().inverse_transforms==6);CHECK(unchanged[0].fits[0].cc==original[0].fits[0].cc);
    }
}
template<class T>void observed_capture_crops(){
    Data data;const std::array<std::size_t,3> starts{0,7,32};const t::Shape shape{span,N};
    constexpr std::size_t captured_first=129,captured_end=N-93;
    std::vector<std::uint8_t> observed(data.pcm.size());
    for(auto i=captured_first;i<captured_end;++i)observed[i]=1;
    std::vector<g::Prefix<1>::Row> packed(N);std::vector<std::array<Complex,2>> rows(N);
    for(std::size_t i=0;i<N;++i){rows[i]=data.rows[i][0];packed[i][0]=rows[i];}
    std::vector<std::uint64_t> points{0,N};std::array<std::pair<std::uint64_t,std::uint64_t>,3> crops;
    for(std::size_t j=0;j<starts.size();++j){const auto lo=captured_first-starts[j],hi=captured_end-starts[j];
        crops[j]={lo,hi};points.push_back(lo);points.push_back(hi);}
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    g::Prefix<1> prefix(data.powers,0x7788,points,budget);prefix.append(0,packed,data.mask);prefix.finish(N);
    std::vector<std::array<g::Gram,2>> grams;
    for(std::size_t j=0;j<starts.size();++j)grams.push_back(prefix.query(crops[j].first,crops[j].second,starts[j],data.reference,true)[0]);
    const g::Override token{grams,starts,0,0x7788,17,data.reference.revision()};
    const g::Binding binding{0x7788,17,data.reference.revision()};
    t::Plan<T> plan(shape);t::PreparedInput<T> input(plan,{data.pcm,data.oscillator,observed,0},budget);
    t::PreparedFragment<T> fragment(plan,{rows,data.mask,0},budget);t::Accumulator<T> baseline(plan,0,starts,budget);
    baseline.add(input,fragment);const auto full=baseline.finish();
    constexpr auto mode=g::Mode::external_gram;
    t::PreparedInput<T> sparse_input(plan,{data.pcm,data.oscillator,observed,0},budget,{},mode);
    t::PreparedFragment<T> sparse_fragment(plan,{rows,data.mask,0},budget,{},mode);
    t::Accumulator<T> reduced(plan,0,starts,budget,{},false,false,mode);reduced.add(sparse_input,sparse_fragment);
    CHECK(reduced.accepts_origin_gram(token,binding));const auto accelerated=reduced.finish({},nullptr,&token,&binding);
    for(std::size_t j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit){
        CHECK(accelerated[j].fits[bit].count==full[j].fits[bit].count&&accelerated[j].fits[bit].count>0);
        CHECK(accelerated[j].fits[bit].energy==full[j].fits[bit].energy);
        CHECK(accelerated[j].fits[bit].xc==full[j].fits[bit].xc&&accelerated[j].fits[bit].xs==full[j].fits[bit].xs);
        const auto allowance=accelerated[j].certificate.gram_radius[bit]+full[j].certificate.gram_radius[bit];
        CHECK(std::abs(accelerated[j].fits[bit].cc-full[j].fits[bit].cc)<=allowance);
        CHECK(std::abs(accelerated[j].fits[bit].ss-full[j].fits[bit].ss)<=allowance);
        CHECK(std::abs(accelerated[j].fits[bit].cs-full[j].fits[bit].cs)<=allowance);
    }
}
int main(){try{powers_and_carrier();prefix_and_crops();actual_accumulator_hook<float>();actual_accumulator_hook<double>();
    observed_capture_crops<float>();observed_capture_crops<double>();
    std::cout<<"origin-axis Gram prefix/rotation tests PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
