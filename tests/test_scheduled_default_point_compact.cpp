#include "scheduled_default_point_bulk.hpp"
#include "scheduled_default_compact_chords.hpp"
#include <bit>
// Keep all maintained dense/source-mask assertions intact in their own target.
// This target additionally compares every retained/finalized bit field against
// the existing dense backend on the same multi-dwell independent PCM fixture.
#define main maintained_default_point_family_main
#include "test_scheduled_default_point_family.cpp"
#undef main
namespace bulk=d::scheduled_default_point_bulk;
namespace compact=d::scheduled_default_compact_chords;
static std::uint64_t identity(const bulk::PendingBulk& p){std::uint64_t h=0;
    for(const auto& row:p.fields)for(const auto& bit:row)for(const auto x:bit)h=(h*131)^std::bit_cast<std::uint64_t>(x);return h;}
static void extend_capture(Fixture& f,std::uint64_t end){const auto old=f.pcm.size();f.pcm.resize(end);
    for(auto n=old;n<end;++n){const auto x=.37*std::sin(.13*n)+.29*std::cos(.041*n)+.07*std::sin(.007*n*n);const auto o=std::polar(1.,.731*n);
        f.pcm[n]={x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};}f.g.capture_end=end;}
struct CompactIdentity {std::uint64_t source=1,revision=3,recipe=7;bool operator==(const CompactIdentity&)const=default;};
static void same_fit(const d::CorrelationFit& a,const d::CorrelationFit& b) {
    check(a.count==b.count,"compact actual observation count changed");
    for(const auto pair:{std::pair{a.xc,b.xc},std::pair{a.xs,b.xs},std::pair{a.cc,b.cc},std::pair{a.ss,b.ss},
                        std::pair{a.cs,b.cs},std::pair{a.energy,b.energy}})
        check(std::bit_cast<std::uint64_t>(pair.first)==std::bit_cast<std::uint64_t>(pair.second),"compact regenerated field differs from dense bits");
}
template<class T>static void compact_paired() {
    Fixture f(3,33);extend_capture(f,f.g.capture_end+512);
    const auto dense=bulk::prepare<T>(f.g,bulk::reservation<T>(f.g),f.cells(),f.visit());
    const auto retained=bulk::prepare<T>(f.g,bulk::reservation<T>(f.g,bulk::Retention::recipe),f.cells(),f.visit(),{},
        static_cast<bulk::NoArchive*>(nullptr),static_cast<d::scheduled_default_point_template_cache::Pool<T>*>(nullptr),bulk::Retention::recipe);
    check(retained.complete&&retained.directory&&retained.directory->sealed()&&retained.chords.empty(),"compact backend retained dense payloads or incomplete tags");
    check(retained.stored_cells()==dense.chords.size()&&identity(retained)==identity(dense),"compact prepare changed FFT fields or template coverage");
    check(retained.working_bytes()<dense.working_bytes(),"compact small fixture retained no storage reduction");
    check(retained.work.peak_bytes<=bulk::reservation<T>(f.g,bulk::Retention::recipe),"compact producer exceeded actual reservation");
    CompactIdentity key;compact::Tile<CompactIdentity> tile(key,compact::Tile<CompactIdentity>::reservation());
    std::uint64_t calls=0;
    const auto raw=[&](std::size_t k){++calls;auto c=f.cell(k);if(c.chord.supported&&!c.chord.observed){c.chord.left={};c.chord.right={};}
        return bulk::Chord{c.chord.left,c.chord.right,c.chord.supported};};
    const auto resolve=[&](std::size_t k){return tile.get(*retained.directory,k,key,raw);};
    // Explicit repeated payload lookup: sparse one-row mask holes need not
    // happen to request any given cell twice during the correction traversal.
    const auto first_payload=resolve(1),repeated_payload=resolve(1);
    check(first_payload.left==repeated_payload.left&&tile.stats.hits==1,"bounded payload cache did not retain exact repeat");
    std::size_t first=0;
    for(const auto through:{3U,9U,19U,33U}) {
        auto g=f.g;g.origin_first+=first;g.outputs=through-first;
        const auto selected=std::span<const observation::Selection>(f.selections).subspan(first,g.outputs);
        const auto expected=bulk::finalize_due_batch(dense,g.origin_first,selected,bulk::finalize_reservation(g),f.visit(),f.fallback());
        const auto actual=bulk::finalize_due_batch(retained,g.origin_first,selected,bulk::finalize_reservation(g),f.visit(),f.fallback(),{},family::Limits{},resolve);
        check(expected.valid==actual.valid&&actual.work.mask_rows==expected.work.mask_rows&&actual.work.original_rows==expected.work.original_rows,"compact changed selection/correction coverage");
        for(std::size_t j=0;j<g.outputs;++j)for(unsigned b=0;b<2;++b)same_fit(actual.whole[j][b],expected.whole[j][b]);
        first=through;
    }
    check(calls>0&&tile.stats.provider_calls==calls&&tile.stats.hits>0,"compact resolver did not exercise bounded payload reuse");
    fails([&]{bulk::finalize_lane(retained,f.g.origin_first,f.selections[0],bulk::finalize_reservation(f.g),f.visit(),f.fallback());},"compact correction succeeded without a frozen source resolver");
    auto wrongkey=key;++wrongkey.recipe;
    fails([&]{tile.get(*retained.directory,1,wrongkey,raw);},"different recipe reused compact payload");
    const auto wrong=[&](std::size_t){return bulk::Chord{};};
    const auto wrongresolve=[&](std::size_t k){return tile.get(*retained.directory,k,key,wrong);};
    fails([&]{bulk::finalize_lane(retained,f.g.origin_first,f.selections[0],bulk::finalize_reservation(f.g),f.visit(),f.fallback(),{},family::Limits{},wrongresolve);},"changed regenerated support was accepted");
    const auto frozen=identity(retained);std::stop_source stop;stop.request_stop();
    fails([&]{bulk::finalize_lane(retained,f.g.origin_first,f.selections[0],bulk::finalize_reservation(f.g),f.visit(),f.fallback(),stop.get_token(),family::Limits{},resolve);},"compact finalize cancellation ignored");
    check(identity(retained)==frozen&&retained.directory->sealed(),"failed optional compact finalize damaged pending fields");
    fails([&]{bulk::prepare<T>(f.g,1,f.cells(),f.visit(),{},static_cast<bulk::NoArchive*>(nullptr),
        static_cast<d::scheduled_default_point_template_cache::Pool<T>*>(nullptr),bulk::Retention::recipe);},"compact producer ignored workspace bound");
}
int main(){try {compact_paired<float>();compact_paired<double>();
    std::cout<<"compact pending bulk preserves dense chosen-chord fields, current selection E/N, bounded regeneration and failure fallback\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
