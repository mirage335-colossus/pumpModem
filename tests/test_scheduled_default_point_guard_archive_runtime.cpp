#include "scheduled_default_point_guard_archive_runtime.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>
namespace d=datapump::modem::detail;
namespace runtime=d::scheduled_default_point_guard_archive_runtime;
namespace storage=d::scheduled_default_point_guard_archive;
namespace events=d::scheduled_default_linear_events;
using datapump::Error;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Fn>bool fails(Fn&& fn){try{fn();return false;}catch(const Error&){return true;}}
storage::Layout layout(){return {.125L,2.5L,0,100,4,6,6,3,2,2};}
events::Geometry geometry() {
    events::Geometry g;g.physical_base=.125L;g.node_spacing=2.5L;g.capture_first=10;g.capture_end=80;
    g.template_cells=4;g.outputs=1;g.origin_first=3;g.step=2;g.fragment_cells=2;
    g.symbol_samples=68;g.chip_samples=17;g.subdivisions=4;return g;
}
runtime::Key key() {
    runtime::Key k;k.bank=1;k.processing=2;k.raw_capture=3;k.capture=100;k.index=4;k.epoch=5;k.phase=6;
    k.anchor_sample=7;k.symbol=68;k.chip=17;k.family=8;k.first=9;k.end=11;k.frequency=12;
    k.template_cells=4;k.step=2;k.subdivisions=4;k.origin=.000716L;k.rate=.9999997L;k.anchor_fraction=.125L;
    k.physical_base=.125L;k.node_spacing=2.5L;k.amplitude=.5;k.fhss_enabled=k.fhss_smooth=true;
    k.fhss_period=2448.4L;k.fhss_quiet=100;k.fhss_support=1;k.fhss_lag=17;return k;
}
std::unique_ptr<runtime::Entry> entry(runtime::Key k=key(),bool seal=true) {
    const auto l=layout();auto out=std::make_unique<runtime::Entry>(k,l,runtime::Entry::reservation(l));
    std::array<events::Cell,4> cells{};
    for(auto& c:cells){c.chord.supported=c.chord.observed=true;c.outer_zero=true;}
    out->archive.install_cells(0,cells);
    std::array<d::scheduled_linear_grid_fft::InputRow,6> rows{};
    std::array<long double,6> energies{};std::array<std::uint64_t,6> counts{};
    out->archive.install_moments(6,rows,energies,counts);
    if(seal)out->archive.seal();return out;
}
void slot_lifetime_and_identity() {
    static_assert(!std::is_copy_constructible_v<runtime::Slot::Pin>);
    static_assert(std::is_move_constructible_v<runtime::Slot::Pin>);
    runtime::Slot slot;auto initial=entry();const auto bytes=initial->working_bytes();
    check(slot.publish(initial)&&!initial&&slot.working_bytes()==bytes,"archive retained charge/publication changed");
    const auto k=key();auto g=geometry();check(slot.matches(k,g),"covering archive rejected compatible actual crop");
    auto altered=k;altered.processing++;check(!slot.matches(altered,g),"processing identity omitted");
    altered=k;altered.raw_capture++;check(!slot.matches(altered,g),"raw-fence identity omitted");
    altered=k;altered.bank++;check(!slot.matches(altered,g),"source bank identity omitted");
    altered=k;altered.capture++;check(!slot.matches(altered,g),"prepared capture identity omitted");
    altered=k;altered.index++;check(!slot.matches(altered,g),"private symbol identity omitted");
    altered=k;altered.family++;check(!slot.matches(altered,g),"family identity omitted");
    altered=k;altered.first++;check(!slot.matches(altered,g),"range identity omitted");
    altered=k;altered.origin+=1e-12L;check(!slot.matches(altered,g),"reference origin tolerance introduced");
    altered=k;altered.rate+=1e-12L;check(!slot.matches(altered,g),"reference clock tolerance introduced");
    altered=k;altered.anchor_fraction+=1e-12L;check(!slot.matches(altered,g),"reference anchor identity omitted");
    altered=k;altered.phase++;check(!slot.matches(altered,g),"private phase identity omitted");
    altered=k;altered.frequency++;check(!slot.matches(altered,g),"carrier identity omitted");
    altered=k;altered.subdivisions*=2;check(!slot.matches(altered,g),"endpoint grid policy omitted");
    altered=k;altered.amplitude*=2;check(!slot.matches(altered,g),"limiter amplitude identity omitted");
    altered=k;altered.fhss_lag+=1e-12L;check(!slot.matches(altered,g),"actual FHSS shape identity omitted");
    auto outside=g;outside.capture_end=101;check(!slot.matches(k,outside),"archive used unknown future capture");
    outside=g;outside.origin_first=5;check(!slot.matches(k,outside),"archive used uncovered lane");
    auto pin=slot.pin(k,g);check(bool(pin)&&slot.borrowed(),"archive borrow not pinned");
    check(pin.archive().cell(0)&&pin.archive().moment(6).row,"published archive fields unavailable");
    auto moved=std::move(pin);check(!pin&&moved&&slot.borrowed(),"archive pin move lost protection");
    auto replacement=entry();check(!slot.publish(replacement)&&replacement,"pinned archive replaced");
    check(!slot.clear()&&slot.working_bytes()==bytes,"pressure cleared a live archive borrow");
    moved={};check(!slot.borrowed(),"archive pin leaked");
    auto incomplete=entry(k,false);check(!slot.publish(incomplete)&&incomplete&&slot.matches(k,g),"incomplete preparation replaced completed archive");
    std::stop_source stop;stop.request_stop();check(fails([&]{slot.publish(replacement,stop.get_token());})&&replacement&&slot.matches(k,g),
        "cancelled publication changed incumbent archive");
    check(slot.clear()&&!slot.working_bytes(),"archive eviction retained an uncharged owner");
    check(slot.publish(replacement)&&!replacement,"completed replacement not publishable");
    check(slot.stats.published==2&&slot.stats.evictions==1&&slot.stats.pinned_eviction_skips==1,"archive lifecycle counters changed");
    check(fails([&]{runtime::Entry undercharged(k,layout(),bytes-1);}),"archive allocated without full precharge");
}
d::CorrelationProjection projection(std::uint64_t n) {
    const auto x=.2+.0001*static_cast<double>(n);const auto carrier=std::polar(1.,.017*static_cast<double>(n));
    return {x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),
        carrier.real()*carrier.imag(),x*x};
}
void exact_fence_projection_cache() {
    runtime::ProjectionCache cache;std::uint64_t loads=0,produced=0;
    const auto loader=[&](std::uint64_t row,auto emit) {
        ++loads;const auto first=row<50?37ULL:50ULL;const auto count=row<50?13U:9U;
        emit(first,count,[&](auto n){++produced;return projection(n);});
    };
    const auto a=cache.get(40,loader);const auto expected=projection(40);
    check(a.xc==expected.xc&&a.xs==expected.xs&&a.energy==expected.energy,"projection cache rounded capture fence start");
    (void)cache.get(38,loader);check(loads==1&&produced==13&&cache.hits==1,"projection cache did not reuse complete shifted raw fence");
    (void)cache.get(53,loader);check(loads==2&&produced==22&&cache.fills==2,"projection cache failed actual fence transition");
    check(fails([&]{(void)cache.get(100,[](auto,auto emit){emit(100,513,[](auto n){return projection(n);});});})&&!cache.ready(),
        "projection cache accepted an uncharged oversized fence");
    check(fails([&]{(void)cache.get(100,[](auto,auto emit){emit(100,1,[](auto n){return projection(n);});emit(101,1,[](auto n){return projection(n);});});})&&!cache.ready(),
        "projection cache published repeated fence metadata");
    std::stop_source stop;
    check(fails([&]{(void)cache.get(100,[&](auto,auto emit){emit(100,3,[&](auto n){if(n==102)stop.request_stop();return projection(n);});},stop.get_token());})&&!cache.ready(),
        "cancelled projection fill published partial fence");
    check(cache.fills==2,"failed projection fills counted as published");
    (void)cache.get(40,loader);check(cache.ready()&&cache.fills==3,"projection retry after cancellation failed");
    check(fails([&]{(void)cache.get(100,[](auto,auto){});})&&!cache.ready(),"missing physical fence became a zero projection");
    check(runtime::ProjectionCache::reservation()>=sizeof(cache),"projection scratch was not included in reservation");
}
}
int main(){try {
    slot_lifetime_and_identity();exact_fence_projection_cache();std::cout<<"Default guard archive runtime source cases passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
