#include "scheduled_acquisition_policy.hpp"
#include "scheduled_polynomial_chip_runtime.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
namespace p=d::scheduled_polynomial_chip;
namespace policy=d::scheduled_acquisition_policy;
static std::size_t predicates=0;
#define CHECK(...) do {++predicates;if(!(__VA_ARGS__))throw std::runtime_error(#__VA_ARGS__);}while(false)
using Path=policy::ColoredDemandPath;
static_assert(policy::colored_demand_path(false,false,true,true,false)==Path::integral_proxy);
static_assert(policy::colored_demand_path(false,false,true,true,true)==Path::fractional_proxy);
static_assert(policy::colored_demand_path(false,false,false,true,false)==Path::unavailable);
static_assert(policy::colored_demand_path(true,false,true,true,false)==Path::unavailable); // explicit0 retains old diagnostics
static_assert(policy::colored_demand_path(true,true,true,true,true)==Path::original_direct);
static_assert(policy::colored_demand_path(true,true,true,false,true)==Path::fractional_proxy);
static_assert(policy::colored_demand_path(false,false,true,false,false)==Path::unavailable);
static void identity_and_models() {
    constexpr std::size_t N=136,C=34;
    g::SourceRows source({-1000,0,0,0,4096},2000,N);
    for(std::size_t i=0;i<N;++i){const auto gain=std::min(1.,double(i+1)/19);
        source.append(i<37?100+i:1000+i-37,
            {gain*std::polar(.7,.11*i),gain*std::polar(.8,-.17*i)},i,gain,1);}
    source.seal();CHECK(!source.prepare_integral_geometry(N,C,false));CHECK(!source.geometry_ready);
    if(!c::coordinate_environment()){CHECK(!source.prepare_integral_geometry(N,C,true));return;}
    CHECK(source.prepare_integral_geometry(N,C,true));CHECK(source.geometry_ready);
    CHECK(source.geometry_mode==g::SourceRows::GeometryMode::integral_original);CHECK(source.geometry_delta==0);
    CHECK(source.geometry_square==0 && source.geometry_l1==0 && source.geometry_gram==0);
    for(std::size_t i=0;i<N;++i)CHECK(source.geometry[i].owner==i/C && !source.geometry[i].membership && source.geometry[i].residual==0);
    p::SourceModels original(source,N);CHECK(original.matches(source));
    CHECK(original.cells.size()==4 && original.cells[0].full && !original.cells[1].full && original.cells[2].full && original.cells[3].full);
    auto translated=source.key;translated.origin+=17;CHECK(source.translation_to(translated)==std::optional<long double>(17));
    translated=source.key;translated.origin-=17;CHECK(source.translation_to(translated)==std::optional<long double>(-17));
    const auto revision=source.geometry_revision;
    source.invalidate_geometry();CHECK(!original.matches(source));
    source.geometry_delta=1e-5L;source.publish_geometry(g::SourceRows::GeometryMode::fractional_proxy);
    CHECK(!original.matches(source));CHECK(source.geometry_revision>revision);
    CHECK(source.prepare_integral_geometry(N,C,true));CHECK(!original.matches(source));
    p::SourceModels rebuilt(source,N);CHECK(rebuilt.matches(source));
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)source.prepare_integral_geometry(N,C,true,stop.get_token());}catch(const std::exception&){cancelled=true;}
    CHECK(cancelled && !source.geometry_ready && !rebuilt.matches(source));
    CHECK(source.prepare_integral_geometry(N,C,true));
    source.source[17]+=.125L;CHECK(!source.prepare_integral_geometry(N,C,true));CHECK(!source.geometry_ready);
    source.source[17]=17;source.mask[17]=0;CHECK(source.prepare_integral_geometry(N,C,true));CHECK(source.geometry[17].owner==g::no_owner);
    p::SourceModels masked(source,N);CHECK(!masked.cells[0].full);
    CHECK(source.working_bytes()<=g::SourceRows::reservation(N));
}
static void paired(bool zero_cell) {
    constexpr std::size_t N=143,C=34;const std::array<std::size_t,4> starts{0,3,7,17};
    const std::array<g::RuntimeProxy::Domain,4> crops{{{0,N},{3,132},{39,N},{90,90}}};
    std::vector<float> pcm(N+starts.back());std::vector<std::complex<double>> carrier(pcm.size());std::vector<std::uint8_t> present(pcm.size(),1);
    std::vector<std::array<std::complex<double>,2>> rows(N);std::vector<std::array<double,2>> norms(N);
    std::vector<std::uint8_t> mask(N,1);std::vector<g::ProxyRow> geometry(N);double energy=0;
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=static_cast<float>(.3*std::sin(i*.071)+.02*std::cos(i*.17));carrier[i]=std::polar(1.,i*.731);energy+=double(pcm[i])*pcm[i];}
    for(std::size_t i=0;i<N;++i){const auto gain=std::min(1.,double(i+1)/60);
        rows[i]={gain*std::polar(.7,i*.11),gain*std::polar(.8,-.17*double(i))};
        if(zero_cell && i/C==2)rows[i]={};
        if(i>=15 && i<20)mask[i]=0;
        geometry[i]=mask[i]?g::ProxyRow{i/C,0,0}:g::ProxyRow{};
        norms[i]={c::norm(rows[i][0]),c::norm(rows[i][1])};}
    const auto union_energy=c::up(energy/c::down(1-c::gamma(pcm.size(),std::numeric_limits<double>::epsilon()/2)));
    const auto run=[&](std::span<const std::uint8_t> observed){
        g::RuntimeProxy proxy(starts.size(),C,union_energy,1e-6,2,512,0,true);
        for(std::size_t first=0;first<N;){const auto n=std::min<std::size_t>(37,N-first),length=n+starts.back();
            std::array<g::RuntimeProxy::Domain,4> domains{};for(std::size_t j=0;j<starts.size();++j)domains[j]={
                crops[j].first>first?std::min(n,crops[j].first-first):0,crops[j].end>first?std::min(n,crops[j].end-first):0};
            // Physical phase is already attached to capture samples. A large
            // jump in q fragment provenance emulates a split source chip.
            const auto physical=first<37?1000+first:2000+first;
            proxy.add({std::span<const float>(pcm).subspan(first,length),std::span<const std::complex<double>>(carrier).subspan(first,length),observed.subspan(first,length),physical},
                {std::span<const std::array<std::complex<double>,2>>(rows).subspan(first,n),std::span<const std::uint8_t>(mask).subspan(first,n),physical},
                starts,std::span<const g::ProxyRow>(geometry).subspan(first,n),{},std::span<const std::array<double,2>>(norms).subspan(first,n),domains);
            first+=n;
        }
        return proxy.finish();
    };
    const auto result=run(present);
    for(std::size_t j=0;j<starts.size();++j){std::array<d::CorrelationChipEvidence,2> original{};std::array<d::CorrelationFit,2> whole{};
        for(std::size_t i=crops[j].first;i<crops[j].end;++i)if(mask[i]){const auto at=i+starts[j];const auto x=double(pcm[at]),a=carrier[at].real(),b=carrier[at].imag();
            d::CorrelationProjection input;input.xc=x*a;input.xs=x*b;input.cc=a*a;input.ss=b*b;input.cs=a*b;input.energy=x*x;
            for(unsigned bit=0;bit<2;++bit){d::CorrelationFit fit;fit.add(input,rows[i][bit],1);original[bit].add_fit(fit,i/C);whole[bit].add(input,rows[i][bit],1);}}
        for(unsigned bit=0;bit<2;++bit){const auto& bound=result[j][bit];const auto tail=d::CorrelationChipEvidence::projection(original[bit].cell);
            if(!zero_cell)CHECK(bound.valid && bound.nested && bound.owner_set_certified);
            // RuntimeProxy deliberately retains conservative non-exact zero
            // handling. It may defer a zero-basis case to original refinement.
            if(bound.valid){CHECK(bound.count==whole[bit].count);CHECK(bound.rank==original[bit].projected_rank+tail.second);
                const auto exact=original[bit].projected_energy+tail.first;CHECK(bound.projected.lo<=exact && exact<=bound.projected.hi);}
        }
    }
    present[1]=0;const auto outside=run(present);CHECK(outside[3][0].valid); // empty domain stays empty
    if(!zero_cell)CHECK(outside[1][0].valid && outside[2][0].valid); // nonempty unaffected domains
    present[1]=1;present[50]=0;const auto missing=run(present);CHECK(!missing[0][0].valid && !missing[1][0].valid);CHECK(missing[3][0].valid);
}
static void singleton_enclosure() {
    const std::array<float,1> pcm{.25f};const std::array<std::complex<double>,1> carrier{{{1,0}}};
    const std::array<std::array<std::complex<double>,2>,1> rows{std::array<std::complex<double>,2>{std::complex<double>{.7,.1},std::complex<double>{.8,-.2}}};
    const std::array<std::uint8_t,1> mask{1};const std::array<std::size_t,1> starts{0};const std::array<g::ProxyRow,1> geometry{{{0,0,0}}};
    g::RuntimeProxy proxy(1,34,1,1e-6,2,512,0,true);proxy.add({pcm,carrier,{},0},{rows,mask,0},starts,geometry);
    const auto result=proxy.finish();
    for(unsigned bit=0;bit<2;++bit){
        datapump::modem::detail::CorrelationFit fit;const auto q=rows[0][bit]*carrier[0];
        fit.xc=pcm[0]*q.real();fit.xs=pcm[0]*q.imag();fit.cc=q.real()*q.real();fit.ss=q.imag()*q.imag();
        fit.cs=q.real()*q.imag();fit.energy=double(pcm[0])*pcm[0];fit.count=1;
        const auto original=datapump::modem::detail::CorrelationChipEvidence::projection(fit);
        CHECK(original.second==1);CHECK(result[0][bit].valid && result[0][bit].nested && result[0][bit].owner_set_certified);
        CHECK(result[0][bit].count==1 && result[0][bit].rank==1);
        CHECK(result[0][bit].projected.lo<=original.first && original.first<=result[0][bit].projected.hi);
        CHECK(result[0][bit].projected.lo<=0); // no useful eigenvector/libm energy lower bound claimed
    }
}
int main(){try{identity_and_models();paired(false);paired(true);singleton_enclosure();std::cout<<"integral colored predicates="<<predicates<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
