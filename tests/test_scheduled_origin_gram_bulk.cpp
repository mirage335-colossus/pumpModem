#include "../src/scheduled_origin_gram.hpp"
#include <cfenv>
#include <iostream>
#include <memory>
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

std::unique_ptr<g::Prefix<3>> prepare(const Data& data,g::Build build,std::span<const std::uint64_t> points){
    auto prefix=std::make_unique<g::Prefix<3>>(data.powers,91,points,budget,4096,build);
    constexpr std::array<std::size_t,7> chunks{1,17,63,64,65,257,1023};std::size_t piece=0;
    for(std::size_t at=0;at<N;){const auto count=std::min(chunks[piece++%chunks.size()],N-at);
        prefix->append(at,std::span<const g::Prefix<3>::Row>(data.rows).subspan(at,count),std::span<const std::uint8_t>(data.mask).subspan(at,count));at+=count;}
    prefix->finish(N);CHECK(prefix->valid());return prefix;
}
void paired(){Data data;const std::array<std::uint64_t,11> points{0,41,83,491,512,2448,2573,7344,8000,9792,N};
    auto original=prepare(data,g::Build::interval_reference,points),bulk=prepare(data,g::Build::native_compensated,points);
    std::uint64_t selected=0;for(auto flag:data.mask)selected+=flag;
    CHECK(bulk->native_rows()==selected&&bulk->native_batches()>0&&bulk->native_null_batches()>0&&original->native_rows()==0);
    CHECK(bulk->construction()==g::Build::native_compensated&&bulk->working_bytes()<=g::Prefix<3>::reservation(points.size()));
    double maximum_error=0,maximum_radius=0,reference_radius=0;
    for(auto origin:std::array<std::size_t,3>{0,7,32})for(std::size_t a=0;a<points.size();++a)for(std::size_t b=a;b<points.size();++b){
        const auto rounding=c::scalar_reference_errors(N,0,16*N,0).gram;
        const auto expected=original->query(points[a],points[b],origin,data.reference,true,0,rounding);
        const auto actual=bulk->query(points[a],points[b],origin,data.reference,true,0,rounding);
        for(std::size_t variant=0;variant<3;++variant){const auto direct=data.direct(variant,origin,points[a],points[b]);
            for(unsigned bit=0;bit<2;++bit){const auto& q=actual[variant][bit];const auto& ref=expected[variant][bit];
                CHECK(q.supported&&ref.supported&&q.count==direct[bit].count&&q.count==ref.count);
                CHECK(direct[bit].count==0 || direct[bit].energy>0); // actual six-field direct projection
                for(const auto [center,truth,other]:std::array<std::array<double,3>,3>{{{q.cc,direct[bit].cc,ref.cc},{q.ss,direct[bit].ss,ref.ss},{q.cs,direct[bit].cs,ref.cs}}}){
                    CHECK(std::abs(center-truth)<=q.radius);CHECK(std::abs(center-other)<=q.radius+ref.radius);
                    maximum_error=std::max(maximum_error,std::abs(center-truth));}
                maximum_radius=std::max(maximum_radius,q.radius);reference_radius=std::max(reference_radius,ref.radius);CHECK(q.radius<.01);
            }
        }
    }
    std::cout<<"native aggregate max_absolute_error="<<maximum_error<<" max_radius="<<maximum_radius
        <<" interval_reference_max_radius="<<reference_radius<<" native_rows="<<bulk->native_rows()<<" batches="<<bulk->native_batches()<<'\n';
    CHECK(!bulk->query(0,N,0,data.reference,false)[0][0].supported);
    Data gap(true);CHECK(!bulk->query(0,N,0,gap.reference,true)[0][0].supported);
}
void sparse_far_physical_offsets(){Data data;constexpr std::uint64_t far=1ULL<<20;
    const std::array<std::uint64_t,3> points{0,far,far+N};g::Prefix<3> bulk(data.powers,91,points,budget,4096,g::Build::native_compensated);
    bulk.append(far,data.rows,data.mask);bulk.finish(far+N);CHECK(bulk.valid());
    const auto result=bulk.query(far,far+N,7-static_cast<std::int64_t>(far),data.reference,true);
    for(std::size_t variant=0;variant<3;++variant){const auto direct=data.direct(variant,7,0,N);for(unsigned bit=0;bit<2;++bit){
        const auto& q=result[variant][bit];const auto allowance=q.radius+c::scalar_reference_errors(N,0,16*N,0).gram;
        CHECK(q.supported&&q.count==direct[bit].count);CHECK(std::abs(q.cc-direct[bit].cc)<=allowance);
        CHECK(std::abs(q.ss-direct[bit].ss)<=allowance&&std::abs(q.cs-direct[bit].cs)<=allowance);
    }}
}
void underflow_and_failure(){g::Powers powers({1,0});g::RawReference reference(powers,17);CHECK(reference.append(0,128,{1,0}));
    const std::array<std::uint64_t,3> points{0,37,128};std::vector<g::Prefix<1>::Row> rows(128);
    for(std::size_t i=0;i<rows.size();++i)rows[i][0]={Complex{1e-155,-1e-160},Complex{-1e-160,1e-155}};
    g::Prefix<1> prefix(powers,19,points,budget,4096,g::Build::native_compensated);prefix.append(0,rows);prefix.finish(128);
    const auto value=prefix.query(0,128,0,reference,true);CHECK(value[0][0].supported&&value[0][0].count==128);
    const double expected=128*1e-310;CHECK(std::abs(value[0][0].cc-expected)<=value[0][0].radius);
    std::stop_source stop;stop.request_stop();g::Prefix<1> cancelled(powers,1,points,budget,4096,g::Build::native_compensated);
    bool rejected=false;try{cancelled.append(0,rows,{},stop.get_token());}catch(const std::exception&){rejected=true;}CHECK(rejected&&!cancelled.valid());
    g::Prefix<1> wrong_environment(powers,1,points,budget,4096,g::Build::native_compensated);const auto round=std::fegetround();
    CHECK(std::fesetround(FE_UPWARD)==0);rejected=false;try{wrong_environment.append(0,rows);}catch(const std::exception&){rejected=true;}
    CHECK(std::fesetround(round)==0);CHECK(rejected&&!wrong_environment.valid());
    const std::array<std::uint64_t,2> delayed_points{0,129};
    g::Prefix<1> wrong_finish(powers,1,delayed_points,budget,4096,g::Build::native_compensated);wrong_finish.append(0,rows);
    CHECK(std::fesetround(FE_DOWNWARD)==0);wrong_finish.finish(129);CHECK(std::fesetround(round)==0);
    CHECK(!wrong_finish.valid()); // restored environment cannot revive a bad checkpoint proof

}
int main(){try{paired();sparse_far_physical_offsets();underflow_and_failure();std::cout<<"native aggregate Gram tests PASS\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
