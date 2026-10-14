#include "../src/scheduled_original_projection_cache.hpp"
#include <complex>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace cache=datapump::modem::detail::scheduled_original_projection;
namespace {
struct Projection {double xc=0,xs=0,cc=0,ss=0,cs=0,energy=0;};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
using Cache=cache::Cache<Projection>;
cache::Key key(std::uint64_t first,std::size_t count,double frequency=128.125) {
    constexpr double tau=2*std::numbers::pi;
    const auto seed=std::polar(1.,static_cast<double>(std::remainder(
        static_cast<long double>(first)*tau*frequency/6000,static_cast<long double>(tau))));
    const auto step=std::polar(1.,tau*frequency/6000);
    return {17,1,100000+first,first,count,6000,std::bit_cast<std::uint64_t>(frequency),
        std::bit_cast<std::uint64_t>(seed.real()),std::bit_cast<std::uint64_t>(seed.imag()),
        std::bit_cast<std::uint64_t>(step.real()),std::bit_cast<std::uint64_t>(step.imag()),0};
}
std::vector<Projection> original(const cache::Key& k,std::span<const float> pcm) {
    // Retain the ORIGINAL expression and update order, independently of cache.
    auto oscillator=std::complex(std::bit_cast<double>(k.seed_real_bits),std::bit_cast<double>(k.seed_imag_bits));
    const auto step=std::complex(std::bit_cast<double>(k.step_real_bits),std::bit_cast<double>(k.step_imag_bits));
    std::vector<Projection> prefix(pcm.size()+1);
    for(std::size_t i=0;i<pcm.size();++i){const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(pcm[i]);
        auto p=prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
        prefix[i+1]=p;oscillator*=step;}
    return prefix;
}
void exact_identity() {
    std::vector<float> pcm(512);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(std::sin(i*.037)*.071);
    const auto identity=key(98765,pcm.size());const auto expected=original(identity,pcm);
    Cache rows(2*1024*1024);check(rows.enabled(),"cache should fit bounded rows");
    check(rows.store(identity,expected,2*1024*1024),"complete original row did not store");
    std::vector<Projection> actual(expected.size());check(rows.copy(identity,actual),"exact row missed");
    check(std::memcmp(actual.data(),expected.data(),expected.size()*sizeof(Projection))==0,"cache changed FP64 prefix bits");
    for(std::size_t i=0;i<expected.size();++i)check(std::memcmp(&actual[i],&expected[i],sizeof(Projection))==0,
        "cache changed independently selected original replay clip");
    const auto miss=[&](cache::Key changed){check(!rows.copy(changed,actual),"different exact identity hit");};
    auto changed=identity;changed.capture_binding++;miss(changed);changed=identity;changed.capture_generation++;miss(changed);
    changed=identity;changed.absolute_first++;miss(changed);changed=identity;changed.processing_first++;miss(changed);
    changed=identity;changed.processing_count--;miss(changed);changed=identity;changed.sample_rate++;miss(changed);
    changed=identity;changed.frequency_bits^=1;miss(changed);changed=identity;changed.seed_real_bits^=1;miss(changed);
    changed=identity;changed.seed_imag_bits^=1;miss(changed);changed=identity;changed.step_real_bits^=1;miss(changed);
    changed=identity;changed.step_imag_bits^=1;miss(changed);changed=identity;changed.environment++;miss(changed);
    check(sizeof(rows)+rows.working_bytes()<=rows.capacity_bytes(),"metadata/payload/transient charge exceeds cap");
    // A contraction cancelled after preparation may reuse this complete row.
    std::fill(actual.begin(),actual.end(),Projection{});check(rows.copy(identity,actual),"cancelled contraction lost immutable row");
    const auto copy_before_trim=actual;rows.clear();check(!rows.copy(identity,actual),"trim retained stale key");
    check(std::memcmp(actual.data(),copy_before_trim.data(),actual.size()*sizeof(Projection))==0,
        "external trim invalidated copied bank scratch");
    check(rows.store(identity,expected,2*1024*1024),"trimmed cache did not rebuild");
    rows.discard_before(identity.absolute_first+identity.processing_count-1);
    check(rows.copy(identity,actual),"partial history retirement invalidated required fence");
    rows.discard_before(identity.absolute_first+identity.processing_count);
    check(!rows.copy(identity,actual),"fully retired original range hit");
}
void pressure_and_collision() {
    const std::vector<float> pcm(512,.025F);Cache rows(128*1024);
    const auto first=key(512,pcm.size());const auto prefix=original(first,pcm);
    check(!rows.store(first,prefix,0) && rows.dynamic_bytes()==0,"optional shortage allocated metadata or failed fallback");
    check(!rows.store(first,std::span(prefix).first(20),1024*1024),"partial prefix published as complete");
    auto invalid=first;invalid.capture_generation=0;check(!rows.store(invalid,prefix,1024*1024),"unbound generation stored");
    for(std::uint64_t n=0;n<128;++n){const auto identity=key(512*n,pcm.size());const auto expected=original(identity,pcm);
        check(rows.store(identity,expected,1024*1024),"bounded collision replacement failed");
        check(sizeof(rows)+rows.working_bytes()<=rows.capacity_bytes(),"collision replacement exceeded total capacity");
        std::vector<Projection> actual(expected.size());check(rows.copy(identity,actual),"new collision row absent");
        check(std::memcmp(actual.data(),expected.data(),actual.size()*sizeof(Projection))==0,"eviction returned previous PCM/seed row");}
    check(rows.entries()<=rows.entry_capacity(),"index exceeded fixed entry capacity");
    Cache too_small(Cache::minimum_enabled_capacity()-1);
    check(!too_small.enabled() && !too_small.store(first,prefix,1024*1024) && too_small.dynamic_bytes()==0,
        "smaller-than-enabled cap allocated storage");
    Cache disabled;check(!disabled.copy(first,std::span<Projection>{}) &&
        !disabled.store(first,prefix,1024*1024) && disabled.working_bytes()==0,"disabled fallback allocated rows");
    rows.set_limit(0);check(rows.dynamic_bytes()==0 && !rows.enabled(),"withdrawn cache retained allocation");
}
void provenance_and_statistics() {
    const std::vector<float> pcm(512,.025F);Cache rows(128*1024);
    const auto identity=key(0,pcm.size());const auto prefix=original(identity,pcm);
    check(rows.store(identity,prefix,1024*1024,{31,false}),"attributed row did not store");
    std::vector<Projection> copy(prefix.size());cache::Provenance producer{77,true};
    check(rows.copy(identity,copy,&producer) && producer==cache::Provenance{31,false},
        "hit did not return immutable acquisition producer");
    check(rows.store(identity,prefix,1024*1024,{32,true}),"duplicate key lookup failed");
    check(rows.copy(identity,copy,&producer) && producer==cache::Provenance{31,false},
        "diagnostic consumer rewrote acquisition producer or numeric identity");
    check(rows.statistics().metadata_allocations==1 && rows.statistics().row_allocations==1 &&
        rows.statistics().row_stores==1,"duplicate store changed allocation/publication counts");
    rows.discard_before(identity.absolute_first+identity.processing_count);
    check(rows.statistics().history_evictions==1 && rows.entries()==0,"history eviction was not counted");
    const auto second=key(512,pcm.size());const auto second_prefix=original(second,pcm);
    check(rows.store(second,second_prefix,1024*1024,{33,true}) && rows.copy(second,copy,&producer) &&
        producer==cache::Provenance{33,true},"diagnostic producer was not preserved");
    check(std::memcmp(copy.data(),second_prefix.data(),copy.size()*sizeof(Projection))==0,
        "provenance changed exact numeric row bytes");
    rows.clear();producer={77,true};
    check(!rows.copy(second,copy,&producer) && producer==cache::Provenance{77,true},
        "miss exposed stale provenance");
    check(!rows.store(second,second_prefix,0,{34,false}) && rows.statistics().budget_skips==1,
        "workspace refusal was not counted");
    for(std::uint64_t n=0;n<128;++n){const auto next=key(512*n,pcm.size());const auto numeric=original(next,pcm);
        check(rows.store(next,numeric,1024*1024,{100+n,false}),"collision attribution store failed");
        check(rows.copy(next,copy,&producer) && producer==cache::Provenance{100+n,false},
            "replacement retained evicted producer");
        check(sizeof(rows)+rows.working_bytes()<=rows.capacity_bytes(),"provenance/statistics exceeded byte cap");}
    check(rows.statistics().collision_evictions==124 && rows.statistics().row_stores==130 &&
        rows.statistics().row_allocations==130 && rows.statistics().metadata_allocations==2,
        "store/eviction counts did not survive optional clear/rebuild");
}
}
int main(){try {exact_identity();pressure_and_collision();provenance_and_statistics();std::cout<<"scheduled ORIGINAL projection cache tests passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
