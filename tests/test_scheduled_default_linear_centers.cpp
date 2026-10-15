#include "scheduled_default_linear_cellstats.hpp"
#define scheduled_default_linear_cellstats scheduled_default_linear_cellstats_reference
#include "scheduled_default_linear_cellstats_reference.hpp"
#undef scheduled_default_linear_cellstats
#include <bit>
#include <iostream>
using namespace datapump;using namespace datapump::modem;
namespace d=datapump::modem::detail;namespace point=d::scheduled_default_linear_cellstats;
namespace reference=d::scheduled_default_linear_cellstats_reference;namespace grid=d::scheduled_linear_grid;
#define CHECK(c) do{if(!(c))throw Error("check failed: " #c);}while(false)
static auto bits(double x){return std::bit_cast<std::uint64_t>(x);}
template<class A,class B>void fits_equal(const A&a,const B&b){CHECK(a.valid==b.valid);
    for(unsigned bit=0;bit<2;++bit){const auto&x=a.fits[bit],&y=b.fits[bit];CHECK(x.count==y.count);
        CHECK(bits(x.xc)==bits(y.xc)&&bits(x.xs)==bits(y.xs)&&bits(x.cc)==bits(y.cc)&&bits(x.ss)==bits(y.ss)&&bits(x.cs)==bits(y.cs)&&bits(x.energy)==bits(y.energy));}}
static void compare(const d::scheduled_linear_grid_fft::InputRow&input,const grid::Cell&cell,double energy,std::uint64_t count){
    const auto old=reference::contract(input,cell,energy,count);const auto diagnostic=point::contract(input,cell,energy,count);const auto centers=point::contract_centers(input,cell,energy,count);
    fits_equal(diagnostic,old);fits_equal(centers,old);CHECK(diagnostic.needs_refinement==old.needs_refinement);
    for(unsigned bit=0;bit<2;++bit)CHECK(static_cast<unsigned>(diagnostic.rank[bit])==static_cast<unsigned>(old.rank[bit]));
    if(centers.valid&&cell.observed&&count){CHECK(centers.needs_refinement);for(const auto rank:centers.rank)CHECK(rank==point::NativeRank::refine);}
    else {CHECK(centers.needs_refinement==diagnostic.needs_refinement);for(unsigned bit=0;bit<2;++bit)CHECK(centers.rank[bit]==diagnostic.rank[bit]);}
}
void physical(){for(const auto scale:{0.,1e-20,.2,1e20,1e30})for(const auto frequency:{0.,.01,.8,1.57})for(const auto n:{1U,2U,3U,17U,73U}){
    point::Moments moments(1,point::Moments::reservation(1));for(unsigned i=0;i<n;++i){const auto t=(i+.375L)/(n+.5L);const auto phase=frequency*i;
        const auto c=std::cos(phase),s=std::sin(phase);const auto x=scale*(.7*std::sin(.37*i)+.3*std::cos(.17*i));
        moments.add(i,t,{x*c,x*s,c*c,s*s,c*s,x*x},1,true);}moments.finish();
    for(const bool zero:{false,true}){grid::Cell cell;cell.supported=cell.observed=true;cell.left={std::complex<double>{.3,.2},std::complex<double>{-.8,.1}};
        cell.right={std::complex<double>{-.1,.7},std::complex<double>{.2,-.3}};if(zero)cell.left=cell.right={};
        compare(moments.rows()[0],cell,static_cast<double>(moments.energy()[0]),moments.count()[0]);}}
}
void edges(){d::scheduled_linear_grid_fft::InputRow input{};grid::Cell cell;cell.supported=cell.observed=true;cell.left=cell.right={std::complex<double>{1,0},std::complex<double>{0,1}};
    compare(input,cell,0,0);compare(input,cell,0,17);input[0]={1,0};compare(input,cell,0,0);input={};cell.observed=false;compare(input,cell,0,0);compare(input,cell,1,0);compare(input,cell,0,1);
    cell.observed=true;cell.supported=false;compare(input,cell,0,0);cell.supported=true;input[5]={0,1};compare(input,cell,0,3);input={};input[0]={INFINITY,0};compare(input,cell,0,3);
    input={};compare(input,cell,-1,3);compare(input,cell,INFINITY,3);compare(input,cell,0,point::maximum_exact_count+1);cell.left[1]={NAN,0};compare(input,cell,0,3);
    cell.left[1]={0,1};input={};input[0]={1e308,1e308};input[5]={1,0};input[6]={1,0};input[7]={1,0};compare(input,cell,1,17);
}
int main(){try{physical();edges();std::cout<<"centers-only bit-identical fits/validity and unchanged diagnostic ranks, fractional physical moments, scale/rank/invalid/quiet/empty coverage passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
