#pragma once
// Exact public metadata only. This helper never shapes private q, reads PCM,
// computes carrier products, grants admission/carry, or changes receiver state.
#include "datapump/sched_hop.hpp"
#include "datapump/fhss_schedule.hpp"
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iterator>
#include <utility>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem::detail::scheduled_original_partition {
inline constexpr auto no_owner=std::numeric_limits<std::uint64_t>::max();
struct Identity {
    std::uint64_t capture=0,processing=0,source=0,source_revision=0;
    std::uint64_t index=0,phase=0,family=0,lane=0;
    bool operator==(const Identity&)const=default;
};
struct Event {
    Identity identity;
    std::uint64_t epoch_sample=0,phase=0,index=0,symbol_samples=0,chip_samples=0;
    SchedHopCoordinate anchor{};
    long double origin=0,rate=1,symbol_source=0;
    std::uint32_t sample_rate=0;
    FhssPowerShape fhss{};
    std::uint64_t begin=0,end=0,native_begin=0,native_end=0,history_first=0,capture_end=0;
    long double displacement=0,left_guard=0,right_guard=0;
};
struct ProcessingFence {
    // Actual Original call cursor/limit, after B/due clipping. Capture fence is
    // separate provenance: it must never be substituted for these boundaries.
    std::uint64_t id=0,capture=0,first=0,end=0;
};
struct Piece {
    Identity identity;
    ProcessingFence fence;
    std::uint64_t first=0,end=0,anchor_first=0;
    SchedHopCoordinate tick0{};
    SchedHopLocation location0{};
    long double source0=0,rate=1;
};
enum class Failure {
    none,event_contract,piece_contract,piece_coverage,native_contract,displacement,
    source_coverage,carry_overflow,invalid_gain,missing_halo,missing_history,
    missing_capture,unproved_gap,capacity,count_overflow
};
struct Evaluation {
    Failure failure=Failure::none;
    std::uint64_t owner=no_owner,carry_owner=no_owner;
    long double raw_source=0,within=0;
    double gain=0;
    SchedHopCoordinate physical{};
    bool current=false,carry=false;
    bool valid()const{return failure==Failure::none;}
};
inline void check_stop(std::stop_token stop){if(stop.stop_requested())throw Error("Original partition cancelled");}
inline bool environment(){return std::fegetround()==FE_TONEAREST && std::numeric_limits<long double>::radix==2;}
inline bool same(SchedHopCoordinate a,SchedHopCoordinate b){return a.sample==b.sample && a.fraction==b.fraction;}
inline bool valid(const Event& e) {
    return environment() && e.identity.capture && e.identity.processing && e.identity.source &&
        e.identity.index==e.index && e.identity.phase==e.phase && e.symbol_samples && e.chip_samples &&
        e.symbol_samples<=1048576 && e.chip_samples<=e.symbol_samples && e.sample_rate &&
        e.index<=std::numeric_limits<std::uint64_t>::max()/(e.symbol_samples/e.chip_samples+(e.symbol_samples%e.chip_samples!=0)) &&
        e.begin<=e.end && e.native_begin<=e.native_end && e.end<=e.capture_end &&
        e.end<0x10000000000000ULL && e.native_end<0x10000000000000ULL &&
        std::isfinite(e.anchor.fraction) && e.anchor.fraction>=0 && e.anchor.fraction<1 &&
        std::isfinite(e.origin) && std::isfinite(e.rate) && e.rate>=.99L && e.rate<=1.01L &&
        std::isfinite(e.symbol_source) && std::isfinite(e.displacement) && e.displacement>=0 && e.displacement<.001L &&
        std::isfinite(e.left_guard) && e.left_guard>=1 && std::isfinite(e.right_guard) && e.right_guard>=1 &&
        (!e.fhss.enabled || (e.fhss.smooth && std::isfinite(e.fhss.support) && e.fhss.support>=1 && e.fhss.support<0x1p52L &&
            std::isfinite(e.fhss.quiet) && e.fhss.quiet>=0 && e.fhss.quiet<0x1p52L && std::isfinite(e.fhss.lag_center)));
}
inline SchedHopCoordinate tick(const Event& e,std::uint64_t p) {
    return SchedHopCoordinate{e.epoch_sample,0}.add_samples(e.phase).add_signed((static_cast<long double>(p)-e.origin)*e.rate);
}
inline std::uint64_t owner_count(const Event& e){return e.symbol_samples/e.chip_samples+(e.symbol_samples%e.chip_samples!=0);}
// One conservative slot preserves the executed rounded within/C cast at S.
inline std::uint64_t owner_slots(const Event& e){return owner_count(e)+1;}
inline bool valid(const Event& e,const Piece& p) {
    if(p.identity!=e.identity || !p.fence.id || !p.fence.capture || p.fence.first>p.first || p.end>p.fence.end ||
       p.first>=p.end || p.anchor_first>p.first || p.anchor_first<p.fence.first || p.rate!=e.rate ||
       !std::isfinite(p.source0) || !same(p.tick0,tick(e,p.anchor_first)))return false;
    if(p.location0.active && p.source0!=p.location0.source.distance(e.anchor)-e.symbol_source)return false;
    return true;
}
// Mapping is the SAME immutable calendar mapping used by Original execution.
// The caller advances cursor through the SAME processing ledger, including
// null calls and its actual process_count limits. No per-row locate is used.
template<class Mapping>
Piece make_piece(const Event& e,ProcessingFence fence,std::uint64_t cursor,Mapping& map,std::stop_token stop={}) {
    check_stop(stop);
    if(!valid(e) || !fence.id || !fence.capture || cursor<fence.first || cursor>=fence.end ||
       cursor<e.begin || fence.end>e.end)throw Error("invalid Original processing piece request");
    Piece p;p.identity=e.identity;p.fence=fence;p.first=p.anchor_first=cursor;p.rate=e.rate;
    p.tick0=tick(e,cursor);p.location0=map.locate(p.tick0);
    long double count;
    if(!p.location0.active) {
        const auto next=map.source_to_physical(p.location0.source,false);
        const auto advance=next.distance(p.tick0)/e.rate;
        count=std::max(1.L,std::ceil(advance));
    } else {
        count=std::max(1.L,std::ceil(SchedHopCoordinate{p.location0.physical_end,0}.distance(p.tick0)/e.rate));
        p.source0=p.location0.source.distance(e.anchor)-e.symbol_source;
    }
    // Preserve Original association and cap BEFORE the uint64 conversion.
    if(!std::isfinite(count))throw Error("Original processing piece extent nonfinite");
    p.end=cursor+static_cast<std::uint64_t>(std::min(count,static_cast<long double>(fence.end-cursor)));
    if(!valid(e,p))throw Error("Original processing piece provenance invalid");
    return p;
}
namespace implementation {
inline Evaluation evaluate(const Event& e,const Piece& p,std::uint64_t physical_row) {
    Evaluation r;
    if(physical_row<p.first || physical_row>=p.end || p.anchor_first>physical_row){r.failure=Failure::piece_contract;return r;}
    if(!p.location0.active)return r;
    // Same Original operations, order and associations. Keep current/carry
    // validation before gain: zero gain cannot hide source/carry exhaustion.
    r.raw_source=p.source0+static_cast<long double>(physical_row-p.anchor_first)*p.rate;
    r.within=r.raw_source;
    if(!std::isfinite(r.within)){r.failure=Failure::source_coverage;return r;}
    if(r.within<0)return r;
    if(r.within>=2.L*e.symbol_samples){r.failure=Failure::source_coverage;return r;}
    r.carry=r.within>=e.symbol_samples;
    if(r.carry)r.within-=e.symbol_samples;
    const auto chips=owner_count(e);
    if((r.carry && e.index==std::numeric_limits<std::uint64_t>::max()) ||
       e.index+(r.carry?1:0)>std::numeric_limits<std::uint64_t>::max()/chips){r.failure=Failure::carry_overflow;return r;}
    r.physical=p.tick0.add_signed(static_cast<long double>(physical_row-p.anchor_first)*p.rate);
    r.gain=SchedHopSourceMap::gain(p.location0,r.physical);
    if(e.fhss.enabled)r.gain*=e.fhss.gain(0,r.physical.sample%(2ULL*e.sample_rate)+r.physical.fraction,e.sample_rate);
    if(!std::isfinite(r.gain) || r.gain<0 || r.gain>1){r.failure=Failure::invalid_gain;return r;}
    if(r.gain==0)return r;
    const auto divided=r.within/static_cast<long double>(e.chip_samples);
    if(!std::isfinite(divided) || divided<0 || divided>chips){r.failure=Failure::source_coverage;return r;}
    const auto owner=static_cast<std::uint64_t>(divided);
    if(r.carry)r.carry_owner=owner;
    else {r.owner=owner;r.current=true;}
    return r;
}
} // namespace implementation
// Safe stand-alone entry point. Repeated execution should bind Evaluator once
// per immutable piece, avoiding repeated scheduled_tick/provenance validation.
inline Evaluation evaluate(const Event& e,const Piece& p,std::uint64_t physical_row) {
    Evaluation result;
    try {
        if(!valid(e)){result.failure=Failure::event_contract;return result;}
        if(!valid(e,p)){result.failure=Failure::piece_contract;return result;}
        return implementation::evaluate(e,p,physical_row);
    } catch(const Error&) {result.failure=Failure::piece_contract;return result;}
}
class Evaluator {
    const Event* event_;const Piece* piece_;
public:
    Evaluator(const Event& event,const Piece& piece):event_(&event),piece_(&piece) {
        if(!valid(event) || !valid(event,piece))throw Error("invalid Original piece evaluator binding");
    }
    Evaluation operator()(std::uint64_t physical_row)const{return implementation::evaluate(*event_,*piece_,physical_row);}
};
inline const Piece* find_piece(std::span<const Piece> pieces,std::uint64_t physical) {
    const auto it=std::upper_bound(pieces.begin(),pieces.end(),physical,[](auto row,const Piece& p){return row<p.first;});
    if(it==pieces.begin())return nullptr;const auto& p=*std::prev(it);return physical<p.end?&p:nullptr;
}
struct Run {std::uint64_t physical_offset=0;std::size_t first=0,count=0;};
template<class RunType=Run> struct NativeView {
    Identity identity;
    std::span<const RunType> runs;
    std::span<const long double> source;
    std::span<const std::uint8_t> mask;
    long double physical_translation=0;
    // Producer includes every active central row in this source interval.
    // Original gained rows must still have an actual stored halo row below.
    long double source_min=-1,source_max=0;
};
struct OwnerCount {std::uint64_t owner=0,count=0;bool operator==(const OwnerCount&)const=default;};
struct RowEdit {
    std::uint64_t physical=0,native_owner=no_owner,original_owner=no_owner;
    std::size_t native_row=0;
    long double original_source=0;
    double original_gain=0;
};
struct Limits {std::size_t max_owners=65536,max_edits=8192,max_gap_rows=8,max_candidates=32768;};
struct PartitionProof {
    Identity identity;
    std::vector<OwnerCount> owners;
    std::vector<RowEdit> edits;
    std::uint64_t native_count=0,original_count=0,rows_evaluated=0,gap_rows_evaluated=0;
    Failure failure=Failure::none;
    bool complete=false;
    std::size_t working_bytes()const{return sizeof(*this)+owners.capacity()*sizeof(OwnerCount)+edits.capacity()*sizeof(RowEdit);}
    static std::size_t reservation(std::size_t owners,Limits limits={}) {
        constexpr auto overhead=sizeof(PartitionProof)+8192;
        if(owners>limits.max_owners || owners>(std::numeric_limits<std::size_t>::max()-overhead)/64 ||
           limits.max_edits>(std::numeric_limits<std::size_t>::max()-overhead-64*owners)/(2*sizeof(RowEdit)))
            return std::numeric_limits<std::size_t>::max();
        return sizeof(PartitionProof)+8192+64*owners+2*limits.max_edits*sizeof(RowEdit);
    }
};
inline bool outside_current(const Event& e,const Piece& p,std::uint64_t first,std::uint64_t end) {
    if(!p.location0.active)return true;
    const auto a=p.source0+static_cast<long double>(first-p.anchor_first)*p.rate;
    const auto b=p.source0+static_cast<long double>(end-1-p.anchor_first)*p.rate;
    if(!std::isfinite(a) || !std::isfinite(b) || b>=2.L*e.symbol_samples)return false;
    // Negative source is skipped; carry still executes its overflow checks.
    if(b>=e.symbol_samples && (e.index==std::numeric_limits<std::uint64_t>::max() ||
        e.index+1>std::numeric_limits<std::uint64_t>::max()/owner_count(e)))return false;
    if(b<0 || a>=e.symbol_samples)return true;
    if(p.location0.continuous)return false;
    const auto left=p.tick0.add_signed(static_cast<long double>(first-p.anchor_first)*p.rate);
    const auto right=p.tick0.add_signed(static_cast<long double>(end-1-p.anchor_first)*p.rate);
    // Exact structural zero regions of SchedHopSourceMap::gain. Fractional
    // rise_begin is not claimed zero; FHSS-only gaps use exact boundary rows.
    return right.sample<p.location0.rise_begin || left.sample>=p.location0.fall_end;
}
// Exact active-row metadata bridge. Sparse-only mask/FHSS certification is NOT
// inferred here. Visitor sees each stored event row and its exact evaluation.
template<class RunType,class Visitor>
PartitionProof partition(const Event& e,std::span<const Piece> pieces,const NativeView<RunType>& native,
                         Visitor visitor,Limits limits={},std::stop_token stop={}) {
    PartitionProof out;out.identity=e.identity;
    const auto fail=[&](Failure reason){out.complete=false;out.failure=reason;out.owners.clear();out.edits.clear();out.original_count=0;return std::move(out);};
    check_stop(stop);
    if(!valid(e))return fail(Failure::event_contract);
    const auto owners=owner_slots(e);
    if(owners>limits.max_owners || !limits.max_edits || !limits.max_gap_rows)return fail(Failure::capacity);
    if(e.index>std::numeric_limits<std::uint64_t>::max()/owner_count(e))return fail(Failure::carry_overflow);
    if(native.identity!=e.identity || native.source.size()!=native.mask.size() || native.source.size()>1048576 ||
       !std::isfinite(native.physical_translation) || std::trunc(native.physical_translation)!=native.physical_translation ||
       std::abs(native.physical_translation)>=0x1p52L || !std::isfinite(native.source_min) || !std::isfinite(native.source_max) ||
       native.source_min> -1 || native.source_max<e.symbol_samples+1.L || native.source_min>native.source_max)
        return fail(Failure::native_contract);
    std::uint64_t cover=e.begin;
    for(const auto& p:pieces){check_stop(stop);if(!valid(e,p) || p.first!=cover || p.end>e.end)return fail(Failure::piece_coverage);cover=p.end;}
    if(cover!=e.end)return fail(Failure::piece_coverage);
    std::vector<std::uint64_t> native_counts(owners),actual_counts(owners);
    out.owners.reserve(owners);out.edits.reserve(limits.max_edits);
    std::size_t next_row=0;std::uint64_t previous_virtual=0;bool have_run=false;
    std::uint64_t gap_cursor=e.begin;
    const auto prove_gap=[&](std::uint64_t first,std::uint64_t end) {
        while(first<end) {
            check_stop(stop);const auto* p=find_piece(pieces,first);if(!p)return Failure::piece_coverage;
            const auto until=std::min(end,p->end);
            if(!outside_current(e,*p,first,until)) {
                if(until-first>limits.max_gap_rows)return Failure::unproved_gap;
                for(auto row=first;row<until;++row){const auto value=implementation::evaluate(e,*p,row);++out.gap_rows_evaluated;
                    if(!value.valid())return value.failure;if(value.current)return Failure::missing_halo;}
            }
            first=until;
        }
        return Failure::none;
    };
    for(const auto& run:native.runs) {
        check_stop(stop);
        if(!run.count || run.first!=next_row || run.count>native.source.size()-next_row ||
           run.physical_offset>=0x1p52L || run.count>0x10000000000000ULL-run.physical_offset ||
           (have_run && run.physical_offset<previous_virtual))return fail(Failure::native_contract);
        previous_virtual=run.physical_offset+run.count;have_run=true;next_row+=run.count;
        for(std::size_t i=0;i<run.count;++i) {
            if((i&255U)==0)check_stop(stop);
            const auto index=run.first+i;const auto source=native.source[index];
            if(!std::isfinite(source) || source<native.source_min || source>native.source_max || native.mask[index]>1)
                return fail(Failure::native_contract);
            const auto physical=static_cast<long double>(run.physical_offset+i)+native.physical_translation;
            if(!std::isfinite(physical) || std::trunc(physical)!=physical || physical>=0x1p52L)return fail(Failure::native_contract);
            if(physical<0)continue;const auto row=static_cast<std::uint64_t>(physical);
            std::uint64_t central=no_owner;
            if(row>=e.native_begin && row<e.native_end && native.mask[index] && source>=0 && source<e.symbol_samples) {
                const auto divided=source/static_cast<long double>(e.chip_samples);
                if(divided<0 || divided>=owners)return fail(Failure::source_coverage);central=static_cast<std::uint64_t>(divided);
                if(row<e.history_first)return fail(Failure::missing_history);if(row>=e.capture_end)return fail(Failure::missing_capture);
                ++native_counts[central];++out.native_count;
            }
            Evaluation exact;
            if(row>=e.begin && row<e.end) {
                if(row>gap_cursor){const auto reason=prove_gap(gap_cursor,row);if(reason!=Failure::none)return fail(reason);}
                gap_cursor=row+1;
                const auto* p=find_piece(pieces,row);if(!p)return fail(Failure::piece_coverage);
                exact=implementation::evaluate(e,*p,row);++out.rows_evaluated;
                if(!exact.valid())return fail(exact.failure);
                if(p->location0.active && std::abs(exact.raw_source-source)>e.displacement)return fail(Failure::displacement);
                if(exact.current) {
                    if(row<e.history_first)return fail(Failure::missing_history);if(row>=e.capture_end)return fail(Failure::missing_capture);
                    ++actual_counts[exact.owner];++out.original_count;
                }
                visitor(index,row,central,exact);
            }
            if(central!=exact.owner) {
                if(out.edits.size()==limits.max_edits)return fail(Failure::capacity);
                out.edits.push_back({row,central,exact.owner,index,exact.raw_source,exact.gain});
            }
        }
    }
    if(next_row!=native.source.size())return fail(Failure::native_contract);
    if(gap_cursor<e.end){const auto reason=prove_gap(gap_cursor,e.end);if(reason!=Failure::none)return fail(reason);}
    // Independently verify the signed edit/count correction, including newly
    // nonempty owners and mask/window entrants. Transfers cancel whole N.
    for(const auto& edit:out.edits) {
        if(edit.native_owner!=no_owner){if(!native_counts[edit.native_owner])return fail(Failure::count_overflow);--native_counts[edit.native_owner];}
        if(edit.original_owner!=no_owner)++native_counts[edit.original_owner];
    }
    if(native_counts!=actual_counts)return fail(Failure::count_overflow);
    for(std::size_t owner=0;owner<owners;++owner)if(actual_counts[owner])out.owners.push_back({owner,actual_counts[owner]});
    check_stop(stop);out.complete=true;
    if(out.working_bytes()>PartitionProof::reservation(owners,limits))return fail(Failure::capacity);
    return out;
}
template<class RunType>
PartitionProof partition(const Event& e,std::span<const Piece> pieces,const NativeView<RunType>& native,
                         Limits limits={},std::stop_token stop={}) {
    return partition(e,pieces,native,[](auto,auto,auto,const auto&){},limits,stop);
}

// Source-window and fine-owner candidates ONLY. Exact gain/FHSS edge coverage
// is deliberately not claimed. This set cannot authorize a sparse partition.
inline std::vector<std::uint64_t> boundary_candidates(const Event& e,std::span<const Piece> pieces,
                                                     Limits limits={},std::stop_token stop={}) {
    if(!valid(e))throw Error("invalid Original boundary candidate contract");
    std::vector<std::uint64_t> result;result.reserve(limits.max_candidates);
    const auto append=[&](std::uint64_t row){if(result.size()==limits.max_candidates)throw Error("Original boundary candidate cap exceeded");result.push_back(row);};
    const auto eps=std::numeric_limits<long double>::epsilon();
    for(const auto& p:pieces) {
        check_stop(stop);if(!valid(e,p))throw Error("invalid Original boundary piece");if(!p.location0.active)continue;
        const auto maximum_delta=static_cast<long double>(p.end-p.anchor_first)*p.rate;
        // Includes evaluation and rounded owner-division reserve, then outward
        // interval operations around the exact-real affine preimage.
        const auto radius=e.displacement+64*eps*(1+std::abs(p.source0)+std::abs(maximum_delta)+e.symbol_samples);
        for(std::uint64_t chip=0;chip<=owner_count(e);++chip) {
            if((chip&255U)==0)check_stop(stop);
            const auto edge=chip==owner_count(e)?static_cast<long double>(e.symbol_samples):static_cast<long double>(chip)*e.chip_samples;
            const auto down=[](long double x){return std::nextafter(x,-std::numeric_limits<long double>::infinity());};
            const auto up=[](long double x){return std::nextafter(x,std::numeric_limits<long double>::infinity());};
            const auto lo=down(static_cast<long double>(p.anchor_first)+down(down(down(edge-radius)-p.source0)/p.rate));
            const auto hi=up(static_cast<long double>(p.anchor_first)+up(up(up(edge+radius)-p.source0)/p.rate));
            const auto a=std::max(static_cast<long double>(p.first),std::ceil(lo)-1);
            const auto b=std::min(static_cast<long double>(p.end-1),std::floor(hi)+1);
            if(!std::isfinite(a) || !std::isfinite(b) || a>b)continue;
            for(auto row=static_cast<std::uint64_t>(a);; ++row){append(row);if(row==static_cast<std::uint64_t>(b))break;}
        }
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());check_stop(stop);return result;
}
} // namespace datapump::modem::detail::scheduled_original_partition
