#include "scheduled_point_source_nodes.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/sched_hop.hpp"
#include <iostream>
#include <stdexcept>
namespace cache=datapump::modem::detail::scheduled_point_source_nodes;
using datapump::Bytes;using datapump::modem::Config;
static void check(bool condition,const char* what){if(!condition)throw std::runtime_error(what);}
static bool same(const cache::Pair& a,const cache::Pair& b){for(unsigned bit=0;bit<2;++bit){
    if(std::bit_cast<std::uint64_t>(a[bit].real())!=std::bit_cast<std::uint64_t>(b[bit].real()) ||
       std::bit_cast<std::uint64_t>(a[bit].imag())!=std::bit_cast<std::uint64_t>(b[bit].imag()))return false;}return true;}
static void identity_and_publication(){
    cache::Key key{13,7,3,87040,34,4};
    check(cache::nodes(key)==10241,"real C34/Q4 node extent changed");
    for(std::size_t j=0;j<*cache::nodes(key);++j){const auto x=static_cast<double>(j*key.chip)/key.subdivisions;
        check(cache::canonical(key,x)==j,"exact canonical source rejected");
        if(j && j+1<*cache::nodes(key))check(!cache::canonical(key,std::nextafter(x,INFINITY)),"one-ulp approximation silently authorized");}
    check(!cache::canonical(key,-0.)&&!cache::canonical(key,-1.)&&!cache::canonical(key,INFINITY),"invalid source alias accepted");
    auto invalid=key;invalid.symbol=1048577;check(!cache::nodes(invalid),"oversized geometry accepted");
    invalid=key;invalid.subdivisions=3;check(!cache::nodes(invalid),"non-dyadic subdivision accepted");
    invalid=key;invalid.chip=0;check(!cache::nodes(invalid),"zero chip accepted");
    cache::Statistics stats;cache::Pool pool(stats);auto pin=pool.create(key,cache::Pool::row_reservation(key));
    check(bool(pin),"new cache row unavailable");
    const cache::Pair pair{{{.1,-.7},{-1.5,.2}}};std::size_t calls=0;
    auto produce=[&]{++calls;return pair;};
    check(same(pin.value(8.5,produce),pair)&&calls==1,"first paired value changed");
    check(same(pin.value(8.5,produce),pair)&&calls==1,"exact canonical value was regenerated");
    check(same(pin.value(std::nextafter(8.5,INFINITY),produce),pair)&&calls==2,"off-grid original fallback missing");
    check(stats.first_fills==1&&stats.canonical_hits==1&&stats.offgrid_fallbacks==1,"canonical counters wrong");
    bool threw=false;try{pin.value(17.,[]{throw std::runtime_error("cancel fixture");return cache::Pair{};});}
    catch(const std::runtime_error&){threw=true;}check(threw,"producer exception swallowed");
    check(same(pin.value(17.,produce),pair)&&calls==3,"throw published an incomplete node");
    auto nonfinite=pair;nonfinite[0]={INFINITY,0};pin.value(25.5,[&]{return nonfinite;});
    check(same(pin.value(25.5,produce),pair)&&calls==4&&stats.nonfinite_fallbacks==1,"nonfinite value published");
    for(unsigned field=0;field<6;++field){auto other=key;switch(field){case 0:++other.epoch;break;case 1:++other.phase;break;
        case 2:++other.index;break;case 3:other.symbol*=2;break;case 4:other.chip*=2;break;default:other.subdivisions=8;}
        check(!pool.find(other),"different template identity borrowed a row");}
}
static void pins_pressure_and_lru(){
    cache::Statistics stats;cache::Key a{1,1,1,256,8,4},b=a,c=a;++b.phase;c.phase+=2;
    const auto row=cache::Pool::row_reservation(a),cap=cache::Pool::metadata_bytes()+2*row;
    cache::Pool pool(stats,cap);check(pool.limit()==2*row,"metadata omitted from total cap");
    auto p=pool.create(a,row),q=pool.create(b,row);check(p&&q,"two-row fixture failed");
    check(!pool.can_fit(row)&&!pool.evict_one(),"pinned row evicted");
    check(!pool.create(c,row),"byte cap exceeded");q={};
    {cache::Pool::Lease lease(pool);check(pool.borrowed(),"container lease not observed");
        check(pool.evict_one(),"unpinned row not evictable");pool.clear_unpinned();check(!pool.empty(),"clear evicted pinned row");}
    p={};auto fresh=pool.create(c,row);check(bool(fresh),"replacement allocation failed");fresh={};
    {auto touch=pool.find(a);check(bool(touch),"retained LRU identity disappeared");}
    check(pool.evict_one()&&!pool.find(c)&&pool.find(a),"LRU replacement ignored touch order");
    pool.clear_unpinned();check(pool.empty()&&!pool.borrowed(),"full trim retained unborrowed rows");
    check(pool.working_bytes()==0&&stats.peak_bytes==cap&&stats.evictions>=3,"actual byte/eviction counters wrong");
    cache::Statistics tiny_stats;cache::Pool tiny(tiny_stats,cache::Pool::metadata_bytes());
    check(!tiny.create(a,row)&&!tiny_stats.created,"metadata-only cap allocated payload");
}
static Config pattern_config(unsigned factor){
    Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=500./factor;
    c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss=factor>1;c.dsss_factor=factor;
    c.outer_dsss_version=datapump::modem::OuterDsssVersion::interleaved_v2;
    c.integration_seconds=std::nextafter(static_cast<double>(32*std::max(64U,2*factor))/c.sample_rate,0.);
    c.data_key.emplace(Bytes(32,0x67));const auto p=c.data_key->stream(datapump::StreamPurpose::Scrambler,0,0,32);
    const auto d=c.data_key->stream(datapump::StreamPurpose::Dsss,0,0,32);
    std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
    auto plan=std::make_shared<datapump::modem::SchedHopPlan>();plan->time_multiplier=3;plan->group_hops=1;
    c.scheduled_hopping=plan;c.stream_epoch=1800000000;return c;
}
static void actual_private_patterns(unsigned factor){
    auto config=pattern_config(factor);datapump::modem::PatternCode code(config,config.stream_epoch);
    cache::Statistics stats;cache::Pool pool(stats);std::uint64_t actual_calls=0;
    for(std::uint64_t phase:{0ULL,136ULL,272ULL})for(std::uint64_t index:{0ULL,1ULL}){
        code.set_stream_phase_samples(phase);const auto first=index*code.chips_per_symbol();code.prepare_symbol(first);
        const cache::Key key{config.stream_epoch,phase,index,code.symbol_samples(),code.chip_samples(),4};
        auto pin=pool.find(key);if(!pin)pin=pool.create(key,cache::Pool::row_reservation(key));check(bool(pin),"private row allocation failed");
        const auto n=*cache::nodes(key);for(std::size_t j=0;j<97;++j){const auto node=j*(n-1)/96;
            const auto source=static_cast<double>(node*key.chip)/key.subdivisions;
            const auto expected=code.shaped_values(first,source);
            const auto produce=[&]{++actual_calls;return code.shaped_values(first,source);};
            check(same(expected,pin.value(source,produce)),"cached private/DSSS waveform differs");
            const auto before=actual_calls;check(same(expected,pin.value(source,produce))&&actual_calls==before,"canonical private hit regenerated");
            // Nonunit receive clocks and carrier-bank selection do not enter
            // this fixed source argument. Both consumers use the SAME value.
            for(const long double rate:{.999999L,1.L,1.000001L}){static_cast<void>(rate);
                check(same(expected,pin.value(source,produce)),"clock/frequency reuse changed raw source value");}
        }
        const auto off=std::nextafter(31.25,INFINITY);const auto before=actual_calls;
        const auto want=code.shaped_values(first,off);check(same(want,pin.value(off,[&]{++actual_calls;return code.shaped_values(first,off);})),"fractional original fallback differs");
        check(actual_calls==before+1,"fractional noncanonical source reused a node");
    }
    check(stats.canonical_hits>stats.first_fills&&stats.offgrid_fallbacks==6,"private cache coverage counters absent");
    check(cache::Pool::metadata_bytes()+pool.working_bytes()<=cache::maximum_bytes,"private cache exceeded total cap");
}
int main(){try{identity_and_publication();pins_pressure_and_lru();for(unsigned factor:{1U,10U,100U,1000U})actual_private_patterns(factor);
    std::cout<<"exact paired source nodes preserve private/DSSS values, fractional fallback, publication, LRU/pins and total workspace caps\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
