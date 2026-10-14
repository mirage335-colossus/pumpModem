#include "scheduled_acquisition_memory.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace datapump::modem::detail::scheduled_acquisition_memory;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
template<class F>void rejects(F f){try{f();}catch(const datapump::Error&){return;}throw std::runtime_error("expected overflow rejection");}
int main(){try {
    for(const auto p:{1ULL,32ULL,8192ULL,65536ULL})for(const auto fixed:{0ULL,33ULL,1000000ULL}) {
        const auto exact=fixed+StablePool::minimum_available(p);
        CHECK(StablePool::minimum_available(p)==10*p);
        CHECK(!StablePool::admits(fixed,exact-1,p));CHECK(StablePool::capacity(exact-1-fixed,p)==4);
        CHECK(StablePool::admits(fixed,exact,p));CHECK(StablePool::capacity(exact-fixed,p)==5);
        CHECK(StablePool::admits(fixed,exact+1,p));CHECK(StablePool::capacity(exact+1-fixed,p)>=5);
        CHECK(!StablePool::admits(exact,exact-1,p));
        CHECK(StablePool::capacity(5000*p,p)==StablePool::maximum_slots);
        CHECK(StablePool::owner_capacity(5000*p,p)==2500);
        CHECK(StablePool::owner_capacity(exact-fixed,p)==5);
        CHECK(StablePool::owner_capacity(exact-1-fixed,p)==4);
    }
    rejects([]{(void)StablePool::minimum_available(0);});
    rejects([]{(void)StablePool::capacity(1,0);});
    rejects([]{(void)StablePool::owner_capacity(1,0);});
    rejects([]{(void)StablePool::owner_capacity(1,std::numeric_limits<std::size_t>::max());});
    rejects([]{(void)StablePool::minimum_available(std::numeric_limits<std::size_t>::max());});
    rejects([]{(void)StablePool::capacity(1,std::numeric_limits<std::size_t>::max());});
    CHECK(!StablePool::admits(std::numeric_limits<std::size_t>::max(),0,1));
    std::cout<<"inclusive5-slot/2x admission boundary passed (pure arithmetic)\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
