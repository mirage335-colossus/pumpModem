#include "shared_scheduled_capture.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
using namespace datapump::modem::detail::scheduled_capture;
namespace {std::atomic<std::size_t> allocations{0};}
void* operator new(std::size_t n) {allocations.fetch_add(1,std::memory_order_relaxed);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
template<class F>void rejects(F f) {try{f();}catch(const Error&){return;}throw std::runtime_error("expected checked layout rejection");}
void bounds_and_no_allocations() {
    for(const auto chunks:{1U,3U,4U,17U})for(const auto consumers:{1U,7U,314U})for(const auto samples:{1U,32U,8192U}) {
        Limits l{16*1024*1024,chunks,consumers,samples};
        const auto fixed=SharedScheduledCapture::required_fixed_bytes(l),slab=samples*sizeof(float)+128;
        const auto before=allocations.load();const auto plan=SharedScheduledCapture::layout(l);
        CHECK(allocations.load()==before);
        CHECK(plan.fixed_bytes==fixed && plan.slab_bytes==slab);
        CHECK(plan.maximum_bytes==fixed+chunks*slab && plan.reserved_bytes==plan.maximum_bytes);
        CHECK(plan.guaranteed_samples==(chunks>3?(chunks-3ULL)*samples:0));
        SharedScheduledCapture actual(l,{21,44},0);
        CHECK(actual.capacity_reservation()==plan.reserved_bytes && actual.guaranteed_retained_samples()==plan.guaranteed_samples);
        for(const auto paid:{0U,1U,3U,4U})if(paid<=chunks) {
            l.workspace_bytes=fixed+paid*slab;
            const auto p=SharedScheduledCapture::layout(l);CHECK(p.reserved_bytes==l.workspace_bytes);
            CHECK(p.guaranteed_samples==(paid>3?(paid-3ULL)*samples:0));
            if(paid>0) {--l.workspace_bytes;const auto below=SharedScheduledCapture::layout(l);
                CHECK(below.guaranteed_samples==((paid-1)>3?(paid-4ULL)*samples:0));}
        }
        l.workspace_bytes=fixed-1;rejects([&]{(void)SharedScheduledCapture::layout(l);});
    }
    for(const auto capacity:{809520800ULL,128ULL*1024*1024}) {
        const auto before=allocations.load();const auto p=SharedScheduledCapture::cohort_layout(capacity,314);
        CHECK(allocations.load()==before);CHECK(p.reserved_bytes<=std::min<std::size_t>(capacity/4,64*1024*1024));
    }
    const auto user=SharedScheduledCapture::cohort_layout(809520800,314);
    // Exact actual-V8 owner trace: capacity67,015,208, consumers314.
    if(const auto* expected=std::getenv("DATAPUMP_EXPECT_NATIVE_CAPTURE_BYTES"))
        CHECK(user.reserved_bytes==std::stoull(expected)); // Optional host-ABI trace replay, not a portable constant.
    CHECK(user.guaranteed_samples>=988929);
    std::cout<<"user capture reservation="<<user.reserved_bytes<<" history capacity="<<user.guaranteed_samples<<'\n';
}
void invalid_and_overflow() {
    Limits l{1024*1024,4,4,32};
    auto z=l;z.maximum_chunks=0;rejects([&]{(void)SharedScheduledCapture::layout(z);});
    z=l;z.maximum_consumers=0;rejects([&]{(void)SharedScheduledCapture::layout(z);});
    z=l;z.chunk_samples=0;rejects([&]{(void)SharedScheduledCapture::layout(z);});
    z=l;z.chunk_samples=std::numeric_limits<std::size_t>::max();rejects([&]{(void)SharedScheduledCapture::layout(z);});
    z=l;z.maximum_chunks=std::numeric_limits<std::size_t>::max();rejects([&]{(void)SharedScheduledCapture::layout(z);});
    z=l;z.maximum_consumers=std::numeric_limits<std::size_t>::max();rejects([&]{(void)SharedScheduledCapture::layout(z);});
    rejects([&]{(void)SharedScheduledCapture::cohort_layout(0,314);});
    rejects([&]{(void)SharedScheduledCapture::cohort_layout(4096,314);});
    rejects([&]{(void)SharedScheduledCapture::cohort_layout(809520800,0);});
}
int main(){try{bounds_and_no_allocations();invalid_and_overflow();std::cout<<"scheduled capture layout checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
