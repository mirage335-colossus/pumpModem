#include "datapump/fast/codec.hpp"
#include <iostream>
#include <iomanip>
using namespace datapump::fast;
int main(){std::cout<<"bytes,encrypted,apsk,rate,robust,depth,intervals,seconds,source_bps,cycles\n";for(auto n:{100000ULL,5000000ULL,50000000ULL})for(bool enc:{false,true})for(unsigned m:{16,64,256})for(auto r:{CodeRate::half,CodeRate::three_quarters,CodeRate::seven_eighths})for(bool robust:{true,false})for(unsigned d=1;d<=64;++d){auto p=profile(Channel::wire);p.constellation=m;p.code_rate=r;p.robust=robust;p.interleave_depth=d;auto e=estimate_transmission(p,enc,n);std::cout<<n<<','<<enc<<','<<m<<','<<code_rate_value(r)<<','<<robust<<','<<d<<','<<e.intervals<<','<<std::setprecision(12)<<e.seconds<<','<<e.source_bps<<','<<e.intervals/cycle_intervals(p)<<'\n';}}
