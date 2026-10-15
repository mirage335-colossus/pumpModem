#include "scheduled_sparse_owned_spans.hpp"
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>

namespace s=datapump::modem::detail::scheduled_sparse_owned_spans;
namespace p=datapump::modem::detail::scheduled_original_partition;
using datapump::modem::SchedHopCoordinate;
using datapump::modem::SchedHopLocation;
using datapump::modem::SchedHopSourceMap;
void check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
struct Map {
    std::uint64_t start=100,active=17000,quiet=23,guard=1,ramp=3;
    bool continuous=false;
    std::uint64_t calls=0;
    SchedHopLocation locate(SchedHopCoordinate physical) {
        ++calls;SchedHopLocation l;
        if(continuous){l.active=l.continuous=true;l.source=physical;l.physical_end=std::numeric_limits<std::uint64_t>::max();return l;}
        if(physical.sample<start){l.source={0,0};l.physical_end=start;return l;}
        const auto group=(physical.sample-start)/(active+quiet),within=(physical.sample-start)%(active+quiet);
        const auto begin=start+group*(active+quiet),source=group*active;
        if(within>=active){l.source={source+active,0};l.physical_end=begin+active+quiet;return l;}
        l.active=true;l.physical_begin=begin;l.physical_end=begin+active;l.source_begin=source;l.source_end=source+active;
        l.rise_begin=begin+guard;l.flat_begin=l.rise_begin+ramp;l.fall_end=begin+active-guard;l.flat_end=l.fall_end-ramp;
        l.source={source+within,physical.fraction};return l;
    }
    SchedHopCoordinate source_to_physical(SchedHopCoordinate source,bool) {
        if(continuous)return source;
        return {start+(source.sample/active)*(active+quiet)+source.sample%active,source.fraction};
    }
};
s::Event event(std::uint64_t samples=136,std::uint64_t chip=34,long double rate=1) {
    s::Event e;e.identity={101,202,303,1,0,0,7,9};e.epoch_sample=100;e.anchor={0,0};
    e.origin=.125L;e.rate=rate;e.symbol_samples=samples;e.chip_samples=chip;e.sample_rate=6000;
    e.displacement=.25L;e.left_guard=e.right_guard=1;e.history_first=0;return e;
}
std::vector<s::Piece> pieces(const s::Event& e,Map& map,std::uint64_t fence_size=512) {
    std::vector<s::Piece> out;std::uint64_t cursor=e.begin,fence_id=1;
    while(cursor<e.end) {
        // Alternate actual processing boundaries, including non-512 fences.
        const auto length=fence_size+(fence_id%3==0?17:0);
        const auto end=cursor+std::min(length,e.end-cursor);
        const s::ProcessingFence fence{fence_id,800+fence_id,cursor,end};
        while(cursor<end){auto piece=s::make_piece(e,fence,cursor,map);out.push_back(piece);cursor=piece.end;}
        ++fence_id;
    }
    return out;
}
struct Value {std::uint64_t owner=p::no_owner;long double source=0;double gain=0;bool active=false;};
// Independent all-active-row Original oracle, deliberately not calling the
// sparse engine or the released evaluator's old displacement/gain<=1 gates.
Value oracle(const s::Event& e,const s::Piece& piece,std::uint64_t row) {
    Value value;if(!piece.location0.active)return value;value.active=true;
    auto source=piece.source0+static_cast<long double>(row-piece.anchor_first)*e.rate;value.source=source;
    if(source<0)return value;
    if(source>=2.L*e.symbol_samples)throw std::runtime_error("oracle source exhausted");
    const auto carry=source>=e.symbol_samples;if(carry)source-=e.symbol_samples;
    if(carry && (e.index==std::numeric_limits<std::uint64_t>::max() ||
       e.index+1>std::numeric_limits<std::uint64_t>::max()/p::owner_count(e)))throw std::runtime_error("oracle carry exhausted");
    const auto physical=piece.tick0.add_signed(static_cast<long double>(row-piece.anchor_first)*e.rate);
    value.gain=SchedHopSourceMap::gain(piece.location0,physical);
    if(value.gain==0 || carry)return value;
    value.owner=static_cast<std::uint64_t>(source/static_cast<long double>(e.chip_samples));return value;
}
struct Native {
    static constexpr std::uint64_t shift=512;
    std::vector<p::Run> runs;
    std::vector<long double> source;
    std::vector<std::uint8_t> mask;
    p::NativeView<> view(const s::Event& e)const{return {e.identity,runs,source,mask,-static_cast<long double>(shift),-1,e.symbol_samples+1.L};}
};
Native native_rows(const s::Event& e,std::span<const s::Piece> all,std::uint64_t omit=p::no_owner) {
    Native out;
    for(auto row=e.begin;row<e.end;++row) {
        const auto* piece=p::find_piece(all,row);check(piece,"native piece missing");
        const auto value=oracle(e,*piece,row);
        if(!value.active || value.source< -1 || value.source>e.symbol_samples+1.L || row==omit)continue;
        if(out.runs.empty() || out.runs.back().physical_offset+out.runs.back().count!=row+Native::shift)
            out.runs.push_back({row+Native::shift,out.source.size(),0});
        ++out.runs.back().count;out.source.push_back(value.source);out.mask.push_back(value.gain>0);
    }
    return out;
}
using Owners=std::map<std::uint64_t,std::uint64_t>;
Owners expanded_original(const s::Event& e,std::span<const s::Piece> all) {
    Owners out;for(auto row=e.begin;row<e.end;++row){const auto value=oracle(e,*p::find_piece(all,row),row);if(value.owner!=p::no_owner)out[row]=value.owner;}return out;
}
Owners expanded_native(const s::Event& e,const Native& n) {
    Owners out;
    for(const auto& run:n.runs)for(std::size_t i=0;i<run.count;++i) {
        const auto physical=run.physical_offset+i-Native::shift;const auto index=run.first+i;
        if(physical<e.native_begin || physical>=e.native_end || !n.mask[index] || n.source[index]<0 || n.source[index]>=e.symbol_samples)continue;
        out[physical]=static_cast<std::uint64_t>(n.source[index]/static_cast<long double>(e.chip_samples));
    }
    return out;
}
std::vector<p::OwnerCount> counts(const Owners& rows) {
    std::map<std::uint64_t,std::uint64_t> bins;for(const auto& [row,owner]:rows){(void)row;++bins[owner];}
    std::vector<p::OwnerCount> out;for(const auto& [owner,count]:bins)out.push_back({owner,count});return out;
}
void compare(const s::Event& e,std::span<const s::Piece> all,const Native& n,const s::Proof& proof) {
    check(proof.complete,"sparse proof incomplete");
    const auto expected=expanded_original(e,all),central=expanded_native(e,n);
    Owners expanded;for(const auto& span:proof.spans) {
        check(span.piece<all.size() && span.first>=all[span.piece].first && span.end<=all[span.piece].end,"span lost Original piece anchor");
        for(auto row=span.first;row<span.end;++row)check(expanded.emplace(row,span.owner).second,"Original spans overlap");
    }
    check(expanded==expected && proof.original_count==expected.size() && proof.owners==counts(expected),"sparse Original support/owner/count mismatch");
    check(proof.native_count==central.size() && proof.native_owners==counts(central),"native crop/count mismatch");
    Owners corrected=central;std::uint64_t edit_rows=0,previous_end=0;bool have=false;
    for(const auto& edit:proof.edits) {
        check(edit.first<edit.end && (!have || previous_end<=edit.first),"edit spans overlap or reverse");
        for(auto row=edit.first;row<edit.end;++row) {
            const auto before=central.find(row),after=expected.find(row);
            const auto a=before==central.end()?p::no_owner:before->second,b=after==expected.end()?p::no_owner:after->second;
            check(a==edit.native_owner && b==edit.original_owner && a!=b,"signed edit changed wrong owner/support");
            if(b==p::no_owner)corrected.erase(row);else corrected[row]=b;++edit_rows;
        }
        previous_end=edit.end;have=true;
    }
    check(corrected==expected && edit_rows==proof.edit_rows,"signed edit closure failed");
    check(proof.working_bytes()<=s::Proof::reservation(p::owner_slots(e)),"proof exceeded reservation");
}
struct Fixture {
    s::Event e;Map original_map,central_map;std::vector<s::Piece> all,central_pieces;Native n;s::NativeInventory inventory;
};
Fixture fixture(std::uint64_t samples,std::uint64_t chip,long double rate,long double source_delta,
        bool continuous=false,std::uint64_t epoch=100,std::uint64_t fence_size=512) {
    Fixture f;f.e=event(samples,chip,rate);f.e.epoch_sample=epoch;
    f.original_map.start=f.central_map.start=epoch;f.original_map.continuous=f.central_map.continuous=continuous;
    if(continuous)f.e.anchor={epoch,0};
    if(samples<1000){f.original_map.active=f.central_map.active=68;f.original_map.quiet=f.central_map.quiet=22;}
    else if(samples<=5120){f.original_map.active=f.central_map.active=997;f.original_map.quiet=f.central_map.quiet=17;}
    const auto target=f.original_map.source_to_physical({continuous?epoch+samples:samples,0},false);
    const auto distance=target.distance({epoch,0});
    f.e.end=f.e.native_end=f.e.capture_end=static_cast<std::uint64_t>(std::ceil(distance/rate+f.e.origin))+2;
    f.all=pieces(f.e,f.original_map,fence_size);
    auto central=f.e;central.origin+=source_delta/rate;
    f.central_pieces=pieces(central,f.central_map,fence_size);
    f.n=native_rows(central,f.central_pieces);f.inventory=s::build_native_inventory(f.e,f.n.view(f.e),rate);
    check(f.inventory.complete,"one-time native inventory failed");return f;
}
void exact_case(std::uint64_t samples,std::uint64_t chip,long double rate,long double delta,bool continuous=false,
        std::uint64_t epoch=100,std::uint64_t fence_size=512) {
    auto f=fixture(samples,chip,rate,delta,continuous,epoch,fence_size);
    const auto map_calls=f.original_map.calls;
    const auto proof=s::partition(f.e,f.all,f.inventory,-static_cast<long double>(Native::shift));
    compare(f.e,f.all,f.n,proof);check(map_calls==f.original_map.calls,"sparse query remapped physical rows");
    // Query probe budget scales with owners/pieces, rather than active samples.
    check(proof.probes.gain<=2*f.all.size(),"gain interior was scanned");
    if(samples>=5120)check(proof.probes.total()<samples,"sparse producer evaluated all active rows");
    struct Borrowed {std::uint64_t physical_first,physical_end,owner;std::size_t row_first;unsigned ignored=42;};
    std::vector<Borrowed> borrowed;for(const auto& span:f.inventory.spans)borrowed.push_back({span.physical_first,span.physical_end,span.owner,span.row_first});
    const s::NativeInventoryView<Borrowed> view{f.inventory.identity,rate,samples,chip,borrowed,true};
    const auto from_view=s::partition(f.e,f.all,view,-static_cast<long double>(Native::shift));compare(f.e,f.all,f.n,from_view);
    check(proof.owners==from_view.owners && proof.edit_rows==from_view.edit_rows,"borrowed native inventory changed query");
}
void cross_clock_case(std::uint64_t samples,std::uint64_t chip,long double native_rate,long double original_rate) {
    auto f=fixture(samples,chip,native_rate,0,true);
    auto target=f.e;target.rate=original_rate;target.origin=-.015625L;
    Map map=f.original_map;map.calls=0;const auto actual=pieces(target,map);
    const auto proof=s::partition(target,actual,f.inventory,-static_cast<long double>(Native::shift));
    compare(target,actual,f.n,proof);
    check(proof.native_rate==native_rate && proof.edit_rows>0,"cross-clock actual inventory lost its rate or edits");
}
void edge_and_failure_cases() {
    auto f=fixture(136,34,1,-.25L,true);
    auto proof=s::partition(f.e,f.all,f.inventory,-static_cast<long double>(Native::shift));compare(f.e,f.all,f.n,proof);
    check(!proof.edits.empty(),"window/chip halo fixture had no membership edits");
    // A gained Original row without native storage remains an explicit edit.
    const auto expected=expanded_original(f.e,f.all);check(!expected.empty(),"missing-row fixture empty");
    const auto missing=expected.begin()->first;auto n=native_rows(f.e,f.all,missing);
    auto inventory=s::build_native_inventory(f.e,n.view(f.e),f.e.rate);
    proof=s::partition(f.e,f.all,inventory,-static_cast<long double>(Native::shift));compare(f.e,f.all,n,proof);
    check(s::native_row(inventory,missing,-static_cast<long double>(Native::shift))==std::numeric_limits<std::size_t>::max(),"missing gained row fabricated native storage");
    check(std::any_of(proof.edits.begin(),proof.edits.end(),[&](const auto& edit){return edit.first<=missing && missing<edit.end && edit.native_owner==p::no_owner;}),"native0 entrant omitted");
    // Entire native support may be empty; Original owners still must exist.
    n.mask.assign(n.mask.size(),0);inventory=s::build_native_inventory(f.e,n.view(f.e),f.e.rate);
    proof=s::partition(f.e,f.all,inventory,-static_cast<long double>(Native::shift));compare(f.e,f.all,n,proof);
    check(proof.native_count==0 && proof.original_count>0,"empty-native Original owner union lost");
    auto cropped=f.e;cropped.begin=7;cropped.end-=3;cropped.native_begin=2;cropped.native_end-=1;
    Map map=f.original_map;map.calls=0;const auto all=pieces(cropped,map,17);
    proof=s::partition(cropped,all,f.inventory,-static_cast<long double>(Native::shift));compare(cropped,all,f.n,proof);
    auto bad=f.e;bad.fhss.enabled=true;proof=s::partition(bad,f.all,f.inventory,-static_cast<long double>(Native::shift));check(!proof.complete && proof.failure==s::Failure::event_contract,"FHSS unproved support accepted");
    bad=f.e;bad.displacement=.25001L;proof=s::partition(bad,f.all,f.inventory,-static_cast<long double>(Native::shift));check(!proof.complete,"out-of-contract displacement accepted");
    auto wrong=f.inventory;wrong.rate=std::numeric_limits<long double>::quiet_NaN();proof=s::partition(f.e,f.all,wrong,-static_cast<long double>(Native::shift));check(!proof.complete && proof.failure==s::Failure::rate_identity,"actual native rate identity lost");
    wrong=f.inventory;++wrong.identity.revision;proof=s::partition(f.e,f.all,wrong,-static_cast<long double>(Native::shift));check(!proof.complete && proof.failure==s::Failure::native_contract,"stale source revision accepted");
    auto bad_rate=f.e;bad_rate.rate=1.0003L;
    check(!s::build_native_inventory(bad_rate,f.n.view(bad_rate),std::numeric_limits<long double>::infinity()).complete,"native producer accepted invalid actual rate");
    auto hole=f.all;hole.erase(hole.begin());proof=s::partition(f.e,hole,f.inventory,-static_cast<long double>(Native::shift));check(!proof.complete && proof.failure==s::Failure::piece_coverage,"processing-ledger hole accepted");
    auto damaged=f.all;damaged.front().source0=std::nextafter(damaged.front().source0,std::numeric_limits<long double>::infinity());
    proof=s::partition(f.e,damaged,f.inventory,-static_cast<long double>(Native::shift));check(!proof.complete,"damaged Original piece accepted");
    s::Limits tiny;tiny.max_probes=1;proof=s::partition(f.e,f.all,f.inventory,-static_cast<long double>(Native::shift),tiny);
    check(!proof.complete && proof.spans.empty() && proof.owners.empty() && proof.edits.empty(),"probe-budget partial proof escaped");
    tiny={};tiny.max_spans=1;proof=s::partition(f.e,f.all,f.inventory,-static_cast<long double>(Native::shift),tiny);check(!proof.complete,"span cap ignored");
    auto absent=f.e;absent.history_first=3;proof=s::partition(absent,f.all,f.inventory,-static_cast<long double>(Native::shift));check(!proof.complete && proof.failure==s::Failure::missing_history,"missing observed history accepted");
    std::stop_source stopped;stopped.request_stop();bool cancelled=false;
    try{(void)s::partition(f.e,f.all,f.inventory,-static_cast<long double>(Native::shift),s::Limits{},stopped.get_token());}catch(const std::exception&){cancelled=true;}
    check(cancelled,"cancellation ignored");proof=s::partition(f.e,f.all,f.inventory,-static_cast<long double>(Native::shift));compare(f.e,f.all,f.n,proof);
    // Source/carry exhaustion must not be hidden by a zero useful mask.
    auto exhausted=f.e;exhausted.symbol_source=-2.L*f.e.symbol_samples;Map m=f.original_map;
    const auto exhausted_pieces=pieces(exhausted,m);
    proof=s::partition(exhausted,exhausted_pieces,f.inventory,-static_cast<long double>(Native::shift));check(!proof.complete && proof.failure==s::Failure::source_coverage,"gain hid Original source exhaustion");
    auto carry=event(8,8,1);carry.index=carry.identity.index=std::numeric_limits<std::uint64_t>::max();
    carry.end=carry.native_end=carry.capture_end=16;
    Map carry_map;carry_map.active=1000;carry_map.guard=100;carry_map.ramp=3;
    const auto carry_pieces=pieces(carry,carry_map);Native empty_native;
    const auto empty_inventory=s::build_native_inventory(carry,empty_native.view(carry),1);
    proof=s::partition(carry,carry_pieces,empty_inventory,-static_cast<long double>(Native::shift));
    check(!proof.complete && proof.failure==s::Failure::carry_overflow,"zero gain hid Original carry overflow");
    // Arbitrarily tiny positive edge fraction can become a zero double gain.
    Map edge;edge.start=100;edge.active=68;edge.quiet=22;edge.guard=1;edge.ramp=3;
    auto e=event(34,8,1);e.epoch_sample=101;e.origin=-std::numeric_limits<long double>::min();e.end=e.native_end=e.capture_end=35;
    const auto edge_pieces=pieces(e,edge,35);auto edge_native=native_rows(e,edge_pieces);
    const auto edge_inventory=s::build_native_inventory(e,edge_native.view(e),1);
    proof=s::partition(e,edge_pieces,edge_inventory,-static_cast<long double>(Native::shift));compare(e,edge_pieces,edge_native,proof);
    check(edge_pieces.front().tick0.fraction>0,"tiny-fraction fixture lost its positive physical residue");
    check(oracle(e,edge_pieces.front(),0).gain==0,"tiny-fraction fixture did not exercise gain underflow");
    // Very long ramps keep interior t^3 normal and positive; no t!=0 guess is
    // used at the fractional endpoints.
    Map long_ramp;long_ramp.active=1ULL<<51;long_ramp.ramp=(1ULL<<50)-2;
    auto ramp=event(136,34,1.0003L);ramp.end=ramp.native_end=ramp.capture_end=139;
    const auto ramp_pieces=pieces(ramp,long_ramp,17);const auto ramp_native=native_rows(ramp,ramp_pieces);
    const auto ramp_inventory=s::build_native_inventory(ramp,ramp_native.view(ramp),ramp.rate);
    proof=s::partition(ramp,ramp_pieces,ramp_inventory,-static_cast<long double>(Native::shift));compare(ramp,ramp_pieces,ramp_native,proof);
    // Exercise exact and neighboring representable chip boundaries with the
    // Original division/cast oracle and processing-piece anchoring.
    for(const auto origin:{0.L,std::nextafter(34.L,35.L)-34.L,std::nextafter(34.L,33.L)-34.L}) {
        auto aligned=event(136,34,1);aligned.origin=origin;aligned.anchor={100,0};
        aligned.end=aligned.native_end=aligned.capture_end=138;Map aligned_map;aligned_map.continuous=true;
        const auto aligned_pieces=pieces(aligned,aligned_map,17);const auto aligned_native=native_rows(aligned,aligned_pieces);
        const auto aligned_inventory=s::build_native_inventory(aligned,aligned_native.view(aligned),1);
        proof=s::partition(aligned,aligned_pieces,aligned_inventory,-static_cast<long double>(Native::shift));compare(aligned,aligned_pieces,aligned_native,proof);
    }
    s::Limits overflow;overflow.max_spans=std::numeric_limits<std::size_t>::max();
    check(s::Proof::reservation(4,overflow)==std::numeric_limits<std::size_t>::max(),"reservation overflow accepted");
}
int main(){try {
    if(!s::environment()){std::cout<<"Sparse Original spans require qualified LD64 round-to-nearest arithmetic\n";return 77;}
    for(const auto rate:{.9997L,1.L,1.0003L,.99L,1.01L})for(const auto delta:{-.25L,-.0004L,0.L,.0004L,.25L}) {
        exact_case(136,34,rate,delta,false,100,17);
        exact_case(5120,8,rate,delta,false);
    }
    exact_case(87040,34,.9997L,-.25L,false);exact_case(87040,34,1.0003L,.25L,false);
    exact_case(87040,34,1.0003L,.25L,true,0x7ffffffff0000ULL);
    exact_case(136,34,.9997L,.25L,true,100,1);
    cross_clock_case(87040,34,1,1+1e-6L);cross_clock_case(5120,8,1,1+2e-5L);cross_clock_case(5120,8,1,1-2e-5L);
    edge_and_failure_cases();std::cout<<"Sparse Original owner-span checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
