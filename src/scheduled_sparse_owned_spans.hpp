#pragma once
// FHSS-off exact Original public membership. No PCM, private q, carrier,
// admission, detector threshold, carry mutation or receiver state lives here.
#include "scheduled_original_boundary_partition.hpp"

namespace datapump::modem::detail::scheduled_sparse_owned_spans {
namespace original=scheduled_original_partition;
using original::Event;
using original::Piece;
using original::Identity;
using original::ProcessingFence;
using original::OwnerCount;
inline constexpr auto no_owner=original::no_owner;

enum class Failure {
    none,event_contract,piece_contract,piece_coverage,source_coverage,carry_overflow,
    gain_geometry,gain_unknown,native_contract,rate_identity,capacity,count_overflow,
    missing_history,missing_capture,count_identity
};
struct Limits {
    std::size_t max_owners=65536,max_spans=16384,max_edits=8192;
    std::uint64_t max_edit_rows=16384,max_probes=262144;
};
struct SourceIdentity {
    std::uint64_t capture=0,source=0,revision=0,index=0,phase=0;
    bool operator==(const SourceIdentity&)const=default;
};
inline SourceIdentity source_identity(const Identity& i) {
    return {i.capture,i.source,i.source_revision,i.index,i.phase};
}
inline bool environment() {
#ifdef __FAST_MATH__
    return false;
#else
    return original::environment() && std::numeric_limits<double>::is_iec559 &&
        std::numeric_limits<double>::radix==2 && std::numeric_limits<double>::digits==53 &&
        std::numeric_limits<long double>::digits>=64;
#endif
}
// The released piece arithmetic does not use displacement. Normalize only its
// old eligibility gate, never any arithmetic operand or provenance identity.
inline Event piece_contract_event(const Event& e) {auto result=e;result.displacement=0;return result;}
inline bool valid(const Event& e) {
    return environment() && !e.fhss.enabled && std::isfinite(e.displacement) &&
        e.displacement>=0 && e.displacement<=.25L && original::valid(piece_contract_event(e));
}
template<class Mapping>
Piece make_piece(const Event& e,ProcessingFence fence,std::uint64_t cursor,Mapping& map,std::stop_token stop={}) {
    if(!scheduled_sparse_owned_spans::valid(e))throw Error("unsupported sparse Original event");
    return original::make_piece(piece_contract_event(e),fence,cursor,map,stop);
}
// Safe by-value bridge for sparse edit-row consumers. The released Evaluator
// stores borrowed pointers, so never bind it to a temporary normalized Event.
// Its conservative gain<=1 check remains intact; bulk membership proves gain
// positive, not an amplitude upper bound. An invalid result requires fallback.
inline original::Evaluation evaluate(const Event& e,const Piece& p,std::uint64_t row) {
    if(!scheduled_sparse_owned_spans::valid(e)){original::Evaluation result;result.failure=original::Failure::event_contract;return result;}
    const auto normalized=piece_contract_event(e);auto result=original::evaluate(normalized,p,row);
    // The Original polynomial is not clamped. Recover only its proved tiny
    // overshoot, completing the owner assignment skipped by the old helper.
    if(result.failure==original::Failure::invalid_gain && std::isfinite(result.gain) &&
       result.gain>1 && result.gain<=1+1024*std::numeric_limits<double>::epsilon()) {
        const auto divided=result.within/static_cast<long double>(e.chip_samples);
        if(!std::isfinite(divided) || divided<0 || divided>original::owner_count(e)) {
            result.failure=original::Failure::source_coverage;return result;
        }
        const auto owner=static_cast<std::uint64_t>(divided);
        if(result.carry)result.carry_owner=owner;else {result.owner=owner;result.current=true;}
        result.failure=original::Failure::none;
    }
    return result;
}

struct NativeSpan {
    // SourceRows' unshifted virtual physical coordinates, not observed PCM.
    std::uint64_t physical_first=0,physical_end=0,owner=0;
    std::size_t row_first=0;
};
template<class SpanType=NativeSpan> struct NativeInventoryView {
    SourceIdentity identity;
    long double rate=0;
    std::uint64_t symbol_samples=0,chip_samples=0;
    std::span<const SpanType> spans;
    bool complete=false;
};
struct NativeInventory {
    SourceIdentity identity;
    long double rate=0;
    std::uint64_t symbol_samples=0,chip_samples=0,selected_count=0;
    std::size_t row_count=0;
    std::vector<NativeSpan> spans;
    std::vector<original::Run> runs;
    Failure failure=Failure::none;
    bool complete=false;
    NativeInventoryView<> view()const{return {identity,rate,symbol_samples,chip_samples,spans,complete};}
    std::size_t working_bytes()const {
        return sizeof(*this)+spans.capacity()*sizeof(NativeSpan)+runs.capacity()*sizeof(original::Run);
    }
    static std::size_t reservation(std::size_t runs,Limits limits={}) {
        const auto maximum=std::numeric_limits<std::size_t>::max();std::size_t n=sizeof(NativeInventory)+8192;
        if(limits.max_spans>(maximum-n)/(2*sizeof(NativeSpan)))return maximum;
        n+=2*limits.max_spans*sizeof(NativeSpan);
        if(runs>(maximum-n)/(2*sizeof(original::Run)))return maximum;
        return n+2*runs*sizeof(original::Run);
    }
};
// Build ONCE from sealed SourceRows' actual source/mask arrays. Query work below
// never scans those arrays. Caller supplies the actual SourceRows key.rate;
// an assumed rate-one value is not a valid substitute for a nonunit source.
template<class RunType>
NativeInventory build_native_inventory(const Event& e,const original::NativeView<RunType>& rows,
        long double actual_source_rate,Limits limits={},std::stop_token stop={}) {
    NativeInventory out;out.identity=source_identity(e.identity);out.rate=actual_source_rate;
    out.symbol_samples=e.symbol_samples;out.chip_samples=e.chip_samples;
    const auto fail=[&](Failure f){out.complete=false;out.failure=f;out.spans.clear();out.runs.clear();out.selected_count=0;return std::move(out);};
    original::check_stop(stop);
    if(!scheduled_sparse_owned_spans::valid(e))return fail(Failure::event_contract);
    if(!std::isfinite(actual_source_rate) || actual_source_rate<.99L || actual_source_rate>1.01L)return fail(Failure::rate_identity);
    if(rows.identity!=e.identity || rows.source.size()!=rows.mask.size() || rows.source.size()>1048576 ||
       original::owner_slots(e)>limits.max_owners || !limits.max_spans)return fail(Failure::native_contract);
    out.spans.reserve(std::min(limits.max_spans,static_cast<std::size_t>(original::owner_slots(e)+rows.runs.size())));
    out.runs.reserve(rows.runs.size());
    std::size_t next=0;std::uint64_t previous_end=0;bool have=false;
    for(const auto& run:rows.runs) {
        original::check_stop(stop);
        if(!run.count || run.first!=next || run.count>rows.source.size()-next ||
           run.physical_offset>=0x10000000000000ULL || run.count>0x10000000000000ULL-run.physical_offset ||
           (have && run.physical_offset<previous_end))return fail(Failure::native_contract);
        out.runs.push_back({run.physical_offset,run.first,run.count});
        for(std::size_t i=0;i<run.count;++i) {
            if((i&255U)==0)original::check_stop(stop);
            const auto index=run.first+i;const auto physical=run.physical_offset+i;
            const auto source=rows.source[index];const auto mask=rows.mask[index];
            if(!std::isfinite(source) || mask>1)return fail(Failure::native_contract);
            if(!mask || source<0 || source>=e.symbol_samples)continue;
            const auto divided=source/static_cast<long double>(e.chip_samples);
            if(!std::isfinite(divided) || divided<0 || divided>=original::owner_slots(e))return fail(Failure::native_contract);
            const auto owner=static_cast<std::uint64_t>(divided);
            if(!out.spans.empty() && out.spans.back().physical_end==physical && out.spans.back().owner==owner &&
               out.spans.back().row_first+out.spans.back().physical_end-out.spans.back().physical_first==index)
                ++out.spans.back().physical_end;
            else {
                if(out.spans.size()==limits.max_spans)return fail(Failure::capacity);
                out.spans.push_back({physical,physical+1,owner,index});
            }
            ++out.selected_count;
        }
        next+=run.count;previous_end=run.physical_offset+run.count;have=true;
    }
    if(next!=rows.source.size())return fail(Failure::native_contract);
    out.row_count=next;original::check_stop(stop);out.complete=true;return out;
}

struct Span {
    std::uint64_t first=0,end=0,owner=0;
    std::size_t piece=0;
};
struct EditSpan {
    std::uint64_t first=0,end=0,native_owner=no_owner,original_owner=no_owner;
};
struct Probes {
    std::uint64_t source=0,physical=0,gain=0,binary=0;
    std::uint64_t total()const{return source+physical+gain;}
};
struct Proof {
    Identity identity;
    SourceIdentity native_identity;
    long double native_rate=0,translation=0;
    std::vector<Span> spans;
    std::vector<OwnerCount> owners,native_owners;
    std::vector<EditSpan> edits;
    std::uint64_t native_count=0,original_count=0,edit_rows=0;
    Probes probes;
    Failure failure=Failure::none;
    bool complete=false;
    std::size_t working_bytes()const {
        return sizeof(*this)+spans.capacity()*sizeof(Span)+(owners.capacity()+native_owners.capacity())*sizeof(OwnerCount)+
            edits.capacity()*sizeof(EditSpan);
    }
    // Includes bounded scratch, projected native spans, count arrays and vector
    // growth up to twice each configured cap. Charge inventory/pieces separately.
    static std::size_t reservation(std::size_t owners,Limits limits={}) {
        const auto maximum=std::numeric_limits<std::size_t>::max();std::size_t n=sizeof(Proof)+8192;
        const auto add=[&](std::size_t count,std::size_t bytes){if(count>(maximum-n)/bytes)return false;n+=count*bytes;return true;};
        if(owners>limits.max_owners || !add(limits.max_spans,4*sizeof(Span)) ||
           !add(limits.max_edits,2*sizeof(EditSpan)) || !add(owners,16*sizeof(std::uint64_t)))return maximum;
        return n;
    }
};
namespace implementation {
struct Exhausted {};
struct Engine {
    const Event& event;const Piece& piece;Proof& out;Limits limits;std::stop_token stop;
    void probe() {
        original::check_stop(stop);
        if(out.probes.total()>=limits.max_probes)throw Exhausted{};
    }
    long double source(std::uint64_t row) {
        probe();++out.probes.source;
        return piece.source0+static_cast<long double>(row-piece.anchor_first)*piece.rate;
    }
    SchedHopCoordinate physical(std::uint64_t row) {
        probe();++out.probes.physical;
        return piece.tick0.add_signed(static_cast<long double>(row-piece.anchor_first)*piece.rate);
    }
    double gain(std::uint64_t row) {
        const auto coordinate=physical(row);probe();++out.probes.gain;
        return SchedHopSourceMap::gain(piece.location0,coordinate);
    }
    template<class Predicate>
    std::uint64_t binary(std::uint64_t first,std::uint64_t end,Predicate predicate) {
        while(first<end) {
            ++out.probes.binary;const auto middle=first+(end-first)/2;
            if(predicate(middle))end=middle;else first=middle+1;
        }
        return first;
    }
    // A rounded affine preimage is ONLY a guess. Exact neighboring predicates
    // establish the boundary; a bad guess falls back to bounded binary search.
    // All predicates used here are monotone under the qualified RN arithmetic.
    template<class Predicate>
    std::uint64_t boundary(std::uint64_t first,std::uint64_t end,long double estimate,Predicate predicate) {
        if(first==end)return first;
        if(!std::isfinite(estimate))return binary(first,end,predicate);
        const auto bounded=std::clamp(std::ceil(estimate),static_cast<long double>(first),static_cast<long double>(end));
        const auto guess=static_cast<std::uint64_t>(bounded);
        const bool right=guess==end || predicate(guess);
        const bool left=guess!=first && predicate(guess-1);
        if(right && !left)return guess;
        if(!right) {
            auto low=guess+1;
            for(unsigned i=0;i<4 && low<end;++i,++low)if(predicate(low))return low;
            return binary(low,end,predicate);
        }
        auto high=guess-1;
        for(unsigned i=0;i<4 && high>first;++i) {
            if(!predicate(high-1))return high;
            --high;
        }
        return binary(first,high,predicate);
    }
    long double source_preimage(long double target)const {
        return static_cast<long double>(piece.anchor_first)+(target-piece.source0)/piece.rate;
    }
    long double physical_preimage(std::uint64_t target)const {
        return static_cast<long double>(piece.anchor_first)+SchedHopCoordinate{target,0}.distance(piece.tick0)/piece.rate;
    }
};
inline bool gain_geometry(const SchedHopLocation& l) {
    if(l.continuous)return true;
    return l.rise_begin<=l.flat_begin && l.flat_begin<l.flat_end && l.flat_end<=l.fall_end &&
        l.fall_end<0x10000000000000ULL;
}
inline bool positive_bulk(Engine& engine,std::uint64_t first,std::uint64_t end) {
    if(end-first<=2 || engine.piece.location0.continuous)return true;
    const auto a=engine.physical(first+1),b=engine.physical(end-2);
    const auto& l=engine.piece.location0;
    // Monotone physical coordinates put every unprobed row between these.
    // Distances are computed in the same qualified long-double environment.
    return a.distance({l.rise_begin,0})>=.49L && SchedHopCoordinate{l.fall_end,0}.distance(b)>=.49L;
}
inline std::pair<std::uint64_t,std::uint64_t> gain_span(Engine& engine,std::uint64_t first,std::uint64_t end,bool& known) {
    known=false;const auto& l=engine.piece.location0;
    if(l.continuous){known=true;return {first,end};}
    if(!gain_geometry(l))return {first,first};
    auto a=engine.boundary(first,end,engine.physical_preimage(l.rise_begin),[&](auto row){return engine.physical(row).sample>=l.rise_begin;});
    auto b=engine.boundary(a,end,engine.physical_preimage(l.fall_end),[&](auto row){return engine.physical(row).sample>=l.fall_end;});
    if(a==b){known=true;return {a,b};}
    if(!positive_bulk(engine,a,b))return {a,a};
    // At most two edge rows can have arbitrarily small fractional ramp input.
    // Evaluate the actual quintic/cast there; never replace it with x>edge.
    const auto first_gain=engine.gain(a);
    if(!std::isfinite(first_gain) || first_gain<0)return {a,a};
    if(b-a==1){known=true;return first_gain>0?std::pair{a,b}:std::pair{b,b};}
    const auto last_gain=engine.gain(b-1);
    if(!std::isfinite(last_gain) || last_gain<0)return {a,a};
    if(first_gain==0)++a;if(last_gain==0)--b;
    known=true;return {a,b};
}
inline bool add_span(std::vector<Span>& spans,Span span,Limits limits) {
    if(span.first==span.end)return true;
    if(!spans.empty() && spans.back().end==span.first && spans.back().owner==span.owner && spans.back().piece==span.piece) {
        spans.back().end=span.end;return true;
    }
    if(spans.size()==limits.max_spans)return false;spans.push_back(span);return true;
}
inline bool add_edit(Proof& out,EditSpan edit,Limits limits) {
    const auto count=edit.end-edit.first;
    if(count>limits.max_edit_rows-out.edit_rows)return false;
    out.edit_rows+=count;
    if(!out.edits.empty() && out.edits.back().end==edit.first && out.edits.back().native_owner==edit.native_owner &&
       out.edits.back().original_owner==edit.original_owner){out.edits.back().end=edit.end;return true;}
    if(out.edits.size()==limits.max_edits)return false;out.edits.push_back(edit);return true;
}
} // namespace implementation

template<class SpanType>
Proof partition(const Event& e,std::span<const Piece> pieces,const NativeInventoryView<SpanType>& native,
        long double physical_translation,Limits limits={},std::stop_token stop={}) {
    Proof out;out.identity=e.identity;out.native_identity=native.identity;out.native_rate=native.rate;out.translation=physical_translation;
    const auto fail=[&](Failure reason){out.complete=false;out.failure=reason;out.spans.clear();out.owners.clear();out.native_owners.clear();
        out.edits.clear();out.original_count=out.native_count=out.edit_rows=0;return std::move(out);};
    original::check_stop(stop);
    if(!scheduled_sparse_owned_spans::valid(e))return fail(Failure::event_contract);
    const auto slots=original::owner_slots(e);
    if(slots>limits.max_owners || !limits.max_spans || !limits.max_edits || !limits.max_probes)return fail(Failure::capacity);
    if(!native.complete || native.identity!=source_identity(e.identity) || native.symbol_samples!=e.symbol_samples ||
       native.chip_samples!=e.chip_samples || !std::isfinite(physical_translation) || std::trunc(physical_translation)!=physical_translation ||
       std::abs(physical_translation)>=0x1p52L)return fail(Failure::native_contract);
    if(!std::isfinite(native.rate) || native.rate<.99L || native.rate>1.01L)return fail(Failure::rate_identity);
    std::uint64_t cover=e.begin;
    for(const auto& piece:pieces) {
        original::check_stop(stop);
        if(!original::valid(e,piece) || piece.first!=cover || piece.end>e.end)return fail(Failure::piece_coverage);
        cover=piece.end;
    }
    if(cover!=e.end)return fail(Failure::piece_coverage);
    std::vector<std::uint64_t> actual(slots),central(slots),corrected(slots);
    std::vector<Span> native_spans;native_spans.reserve(std::min(limits.max_spans,native.spans.size()));
    out.spans.reserve(std::min(limits.max_spans,static_cast<std::size_t>(slots+pieces.size())));
    out.owners.reserve(slots);out.native_owners.reserve(slots);out.edits.reserve(limits.max_edits);
    std::uint64_t previous_native_end=0;std::size_t previous_native_row_end=0;bool have_native=false;
    for(const auto& span:native.spans) {
        original::check_stop(stop);
        if(span.physical_first>=span.physical_end || span.physical_end>=0x1p52L || span.owner>=slots ||
           span.row_first>1048576 || span.physical_end-span.physical_first>1048576-span.row_first ||
           (have_native && (span.physical_first<previous_native_end || span.row_first<previous_native_row_end)))
            return fail(Failure::native_contract);
        previous_native_end=span.physical_end;
        previous_native_row_end=span.row_first+static_cast<std::size_t>(span.physical_end-span.physical_first);have_native=true;
        const auto first=std::max(static_cast<long double>(span.physical_first)+physical_translation,static_cast<long double>(e.native_begin));
        const auto end=std::min(static_cast<long double>(span.physical_end)+physical_translation,static_cast<long double>(e.native_end));
        if(first>=end)continue;
        if(!std::isfinite(first) || !std::isfinite(end) || first<0 || end>=0x1p52L || std::trunc(first)!=first || std::trunc(end)!=end)
            return fail(Failure::native_contract);
        const auto a=static_cast<std::uint64_t>(first),b=static_cast<std::uint64_t>(end);
        if(a<e.history_first)return fail(Failure::missing_history);
        if(b>e.capture_end)return fail(Failure::missing_capture);
        if(!native_spans.empty() && a<native_spans.back().end)return fail(Failure::native_contract);
        if(!implementation::add_span(native_spans,{a,b,span.owner,0},limits))return fail(Failure::capacity);
        central[span.owner]+=b-a;out.native_count+=b-a;
    }
    try {
        for(std::size_t i=0;i<pieces.size();++i) {
            const auto& piece=pieces[i];if(!piece.location0.active)continue;
            implementation::Engine engine{e,piece,out,limits,stop};
            const auto first_source=engine.source(piece.first),last_source=engine.source(piece.end-1);
            if(!std::isfinite(first_source) || !std::isfinite(last_source) || first_source>last_source ||
               last_source>=2.L*e.symbol_samples)return fail(Failure::source_coverage);
            // Original tests coverage/carry BEFORE gain, even on zero rows.
            if(last_source>=e.symbol_samples && (e.index==std::numeric_limits<std::uint64_t>::max() ||
               e.index+1>std::numeric_limits<std::uint64_t>::max()/original::owner_count(e)))return fail(Failure::carry_overflow);
            auto first=engine.boundary(piece.first,piece.end,engine.source_preimage(0),[&](auto row){return engine.source(row)>=0;});
            auto end=engine.boundary(first,piece.end,engine.source_preimage(e.symbol_samples),[&](auto row){return engine.source(row)>=e.symbol_samples;});
            bool gain_known=false;const auto selected=implementation::gain_span(engine,first,end,gain_known);
            if(!gain_known)return fail(implementation::gain_geometry(piece.location0)?Failure::gain_unknown:Failure::gain_geometry);
            first=selected.first;end=selected.second;
            if(first<end && first<e.history_first)return fail(Failure::missing_history);
            while(first<end) {
                const auto divided=engine.source(first)/static_cast<long double>(e.chip_samples);
                if(!std::isfinite(divided) || divided<0 || divided>=slots)return fail(Failure::source_coverage);
                const auto owner=static_cast<std::uint64_t>(divided);
                const auto next=engine.boundary(first,end,engine.source_preimage(static_cast<long double>(owner+1)*e.chip_samples),
                    [&](auto row){return engine.source(row)/static_cast<long double>(e.chip_samples)>=static_cast<long double>(owner+1);});
                if(next<=first)return fail(Failure::source_coverage);
                if(!implementation::add_span(out.spans,{first,next,owner,i},limits))return fail(Failure::capacity);
                actual[owner]+=next-first;out.original_count+=next-first;first=next;
            }
        }
    } catch(const implementation::Exhausted&) {return fail(Failure::capacity);}
    // Span inventories are physical and disjoint; merge their event endpoints.
    // Gaps have no owner. This emits native-zero entrants and source0/S halos
    // without assuming an actual cached row exists for the gained support.
    std::size_t a=0,b=0;std::uint64_t cursor=0;bool started=false;
    while(a<native_spans.size() || b<out.spans.size()) {
        original::check_stop(stop);
        const auto next_a=a<native_spans.size()?native_spans[a].first:no_owner;
        const auto next_b=b<out.spans.size()?out.spans[b].first:no_owner;
        if(!started){cursor=std::min(next_a,next_b);started=true;}
        while(a<native_spans.size() && native_spans[a].end<=cursor)++a;
        while(b<out.spans.size() && out.spans[b].end<=cursor)++b;
        const bool has_a=a<native_spans.size() && native_spans[a].first<=cursor;
        const bool has_b=b<out.spans.size() && out.spans[b].first<=cursor;
        if(!has_a && !has_b) {
            const auto na=a<native_spans.size()?native_spans[a].first:no_owner;
            const auto nb=b<out.spans.size()?out.spans[b].first:no_owner;
            cursor=std::min(na,nb);if(cursor==no_owner)break;continue;
        }
        const auto owner_a=has_a?native_spans[a].owner:no_owner,owner_b=has_b?out.spans[b].owner:no_owner;
        const auto end_a=a<native_spans.size()?(has_a?native_spans[a].end:native_spans[a].first):no_owner;
        const auto end_b=b<out.spans.size()?(has_b?out.spans[b].end:out.spans[b].first):no_owner;
        const auto end=std::min(end_a,end_b);
        if(end<=cursor)return fail(Failure::count_identity);
        if(owner_a!=owner_b && !implementation::add_edit(out,{cursor,end,owner_a,owner_b},limits))return fail(Failure::capacity);
        cursor=end;
    }
    corrected=central;
    for(const auto& edit:out.edits) {
        const auto count=edit.end-edit.first;
        if(edit.native_owner!=no_owner) {
            if(corrected[edit.native_owner]<count)return fail(Failure::count_identity);
            corrected[edit.native_owner]-=count;
        }
        if(edit.original_owner!=no_owner)corrected[edit.original_owner]+=count;
    }
    if(corrected!=actual)return fail(Failure::count_identity);
    for(std::size_t owner=0;owner<slots;++owner) {
        if(actual[owner])out.owners.push_back({owner,actual[owner]});
        if(central[owner])out.native_owners.push_back({owner,central[owner]});
    }
    original::check_stop(stop);out.complete=true;return out;
}

inline Proof partition(const Event& e,std::span<const Piece> pieces,const NativeInventory& native,
        long double physical_translation,Limits limits={},std::stop_token stop={}) {
    return partition(e,pieces,native.view(),physical_translation,limits,stop);
}

// Lookup is optional: no_owner means the gained Original row needs a qualified
// local producer. It never means the physical observation/owner may be omitted.
inline std::size_t native_row(const NativeInventory& inventory,std::uint64_t physical,long double translation) {
    const auto value=static_cast<long double>(physical)-translation;
    if(!std::isfinite(value) || value<0 || value>=0x1p52L || std::trunc(value)!=value)return std::numeric_limits<std::size_t>::max();
    const auto row=static_cast<std::uint64_t>(value);
    const auto it=std::upper_bound(inventory.runs.begin(),inventory.runs.end(),row,[](auto p,const auto& run){return p<run.physical_offset;});
    if(it==inventory.runs.begin())return std::numeric_limits<std::size_t>::max();
    const auto& run=*std::prev(it);
    return row-run.physical_offset<run.count?run.first+static_cast<std::size_t>(row-run.physical_offset):std::numeric_limits<std::size_t>::max();
}
} // namespace datapump::modem::detail::scheduled_sparse_owned_spans
