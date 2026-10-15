#include "scheduled_default_point_fft_runtime.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace runtime=d::scheduled_default_point_fft_runtime;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::unique_ptr<runtime::Entry> entry(std::uint64_t capture,std::size_t family) {
    auto e=std::make_unique<runtime::Entry>();e->key.capture=capture;e->key.processing=7;e->key.family=family;
    e->whole.resize(2);e->valid={1,0};e->processing_end=1024;e->reserved=runtime::Entry::reservation(2);
    for(auto& pair:e->whole)for(auto& f:pair){f.count=34;f.cc=f.ss=17;f.energy=100;f.xc=1;}
    return e;
}
void cache_identity_and_scores() {
    runtime::Pool pool;auto a=entry(5,3);const auto key=a->key;pool.touch(*a);pool.entries[pool.victim()]=std::move(a);
    const auto slot=pool.find(key);check(slot<pool.entries.size(),"exact family cache key not found");
    check(runtime::score(*pool.entries[slot],0).ready,"completed whole point fit not reusable");
    check(!runtime::score(*pool.entries[slot],1).ready&&!runtime::score(*pool.entries[slot],2).ready,"singular/missing cached lane became rejection");
    auto changed=key;++changed.processing;check(pool.find(changed)==pool.entries.size(),"cache reused different processing journal");
    changed=key;++changed.index;check(pool.find(changed)==pool.entries.size(),"cache reused different private symbol");
    changed=key;changed.node_spacing=1;check(pool.find(changed)==pool.entries.size(),"cache reused different global grid");
    // Ledger growth is an append-only stamp check, not part of the grid key.
    check(pool.entries[slot]->processing_end<2048&&pool.find(key)==slot,"ledger growth erased immutable completed prefix");
    const auto charge=pool.working_bytes();check(charge==pool.entries[slot]->reserved,"retained whole fields not charged");
    pool.clear();check(pool.working_bytes()==0&&pool.find(key)==pool.entries.size(),"optional cache clear retained allocations");
}
void bounded_many_family_lru() {
    runtime::Pool pool;
    for(std::size_t i=0;i<runtime::cache_slots;++i){auto e=entry(1,i);pool.touch(*e);pool.entries[pool.victim()]=std::move(e);}
    auto recent=pool.entries[0]->key;pool.touch(*pool.entries[0]);
    const auto victim=pool.victim();check(victim!=0,"many-family LRU evicted latest used family");
    auto e=entry(1,1000);const auto key=e->key;pool.touch(*e);pool.entries[victim]=std::move(e);
    check(pool.find(key)<runtime::cache_slots&&pool.find(recent)<runtime::cache_slots,"bounded LRU discarded retained current family");
    pool.sequence=UINT64_MAX;pool.touch(*pool.entries[0]);check(pool.sequence==1,"LRU sequence overflow changed numeric data or chronology");
}
}
int main(){try{cache_identity_and_scores();bounded_many_family_lru();std::cout<<"Default point FFT runtime source cases passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
