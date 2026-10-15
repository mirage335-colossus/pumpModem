#include "scheduled_linear_fragment.hpp"
#include "scheduled_linear_family.hpp"
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace datapump;using namespace datapump::modem;
namespace d=datapump::modem::detail;namespace fragment=d::scheduled_linear_fragment;
namespace family=d::scheduled_linear_family;namespace grid=d::scheduled_linear_grid;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
template<class F>void rejects(F&&f){bool bad=false;try{f();}catch(const Error&){bad=true;}CHECK(bad);}
void parser(){CHECK(fragment::select(nullptr)==2048);for(const auto text:{"2048","8192","16384"}){const auto f=fragment::select(text);CHECK(f==std::stoull(text));CHECK(fragment::effective(f,17)==17);CHECK(fragment::effective(f,100000)==f);}
    for(const auto text:{"","0","4096","32768","8192junk","+8192"})rejects([&]{fragment::select(text);});rejects([]{fragment::effective(4096,99);});rejects([]{fragment::effective(2048,0);});}
static grid::Pair node(std::size_t n){return {{{.31*std::sin(.031*n),.27*std::cos(.047*n)},{-.21*std::cos(.063*n),.37*std::sin(.053*n)}}};}
template<class T>void execution(std::size_t selected){constexpr std::size_t N=2051;constexpr std::size_t budget=16*1024*1024;
    const auto f=fragment::effective(selected,N);family::Geometry g{.125L,.75L,0,1600,N,3,4,f,{}};
    std::array<std::array<d::CorrelationFit,2>,3>expected{};std::array<std::complex<double>,1600>osc{};std::array<float,1600>pcm{};
    const auto rotation=std::polar(1.,.19);for(std::size_t first=0;first<pcm.size();first+=64){auto o=std::polar(1.,std::remainder(first*.19,2*std::numbers::pi));
        for(std::size_t n=first;n<std::min(first+64,pcm.size());++n){pcm[n]=float(.2*std::sin(.071*n)+.13*std::cos(.37*n));osc[n]=o;o*=rotation;}}
    const auto projection=[&](std::uint64_t n){const auto x=double(pcm[n]);const auto o=osc[n];return d::CorrelationProjection{x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};};
    for(std::size_t j=0;j<g.outputs;++j)for(std::size_t n=0;n<pcm.size();++n){if(!(n%13))continue;const auto u=(n-g.physical_base)/g.node_spacing-j*g.step;
        if(!(u>=0&&u<N))continue;const auto at=std::size_t(std::floor(u));const auto t=double(u-at);const auto a=node(at),b=node(at+1);
        for(unsigned bit=0;bit<2;++bit)expected[j][bit].add(projection(n),(1-t)*a[bit]+t*b[bit],1);}
    const auto cell=[&](auto k,auto,auto){grid::Cell c;c.supported=c.observed=true;c.left=node(k);c.right=node(k+1);c.residual={0,0};return c;};
    const auto visit=[&](auto first,auto end,auto emit){for(auto n=first;n<end;++n)if(n%13)emit(n,projection(n));};
    const auto never_original=[](auto,auto)->family::OriginalRow{throw Error("supported fragment unexpectedly Original");};
    const auto out=family::run<T>(g,budget,cell,visit,never_original);
    CHECK(out.work.transforms.partitions==(selected==2048?2:1));CHECK(out.work.supported_cells==N&&out.work.original_cells==0&&out.work.original_visits==0);
    CHECK(out.work.peak_bytes<=budget&&out.work.transforms.inverse_ffts==16);for(std::size_t j=0;j<g.outputs;++j){const auto&value=out.values[j];CHECK(value.certificate.valid);
        for(unsigned b=0;b<2;++b){const auto&a=value.fits[b],&e=expected[j][b];CHECK(a.count==e.count);
            CHECK(std::abs(a.xc-e.xc)<=value.certificate.dot_radius[b]);CHECK(std::abs(a.xs-e.xs)<=value.certificate.dot_radius[b]);
            CHECK(std::abs(a.cc-e.cc)<=value.certificate.gram_radius[b]);CHECK(std::abs(a.ss-e.ss)<=value.certificate.gram_radius[b]);CHECK(std::abs(a.cs-e.cs)<=value.certificate.gram_radius[b]);
            CHECK(e.energy>=value.certificate.energy_lower&&e.energy<=value.certificate.energy_upper);}}
    unsigned callbacks=0;rejects([&]{family::run<T>(g,1,[&](auto k,auto a,auto b){++callbacks;return cell(k,a,b);},visit,never_original);});CHECK(callbacks==0);
}
int main(){try{parser();for(const auto f:{2048U,8192U,16384U}){execution<float>(f);execution<double>(f);}std::cout<<"bounded fragment parser, exact extent, partitioned I/Q/Gram/count/energy oracle and preflight passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
