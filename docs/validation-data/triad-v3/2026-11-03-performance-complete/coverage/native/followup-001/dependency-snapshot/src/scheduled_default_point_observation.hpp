#pragma once
// Exact Original physical support only: no q, PCM, carrier, owners or score.
#include "scheduled_sparse_owned_spans.hpp"
#include "scheduled_default_linear_runtime.hpp"
#include <vector>
#include <span>
#include <stop_token>
#include <cstdint>
#include <limits>
namespace datapump::modem::detail::scheduled_default_point_observation {
namespace original=scheduled_original_partition;
namespace sparse=scheduled_sparse_owned_spans;
namespace runtime=scheduled_default_linear_runtime;
struct Run {std::uint64_t first=0,end=0;bool operator==(const Run&)const=default;};
enum class Failure {none,event,piece_coverage,source_coverage,carry_overflow,gain_unknown,history,capture,capacity,count_overflow};
struct Selection {
    original::Identity identity;
    std::vector<Run> runs;
    std::uint64_t count=0,probes=0,fhss_nodes=0,fhss_rows=0,fhss_flat_spans=0,fhss_zero_spans=0;
    Failure failure=Failure::none;
    bool valid=false;
    std::size_t working_bytes()const{return sizeof(*this)+runs.capacity()*sizeof(Run);}
    static std::size_t reservation(std::size_t maximum_runs) {
        if(maximum_runs>(std::numeric_limits<std::size_t>::max()-sizeof(Selection)-4096)/(2*sizeof(Run)))
            return std::numeric_limits<std::size_t>::max();
        return sizeof(Selection)+4096+2*maximum_runs*sizeof(Run);
    }
    bool contains(std::uint64_t n)const {
        const auto it=std::upper_bound(runs.begin(),runs.end(),n,[](auto row,const Run& run){return row<run.first;});
        return it!=runs.begin()&&n<std::prev(it)->end;
    }
};
struct Limits {std::size_t maximum_runs=8192;std::uint64_t maximum_probes=262144;unsigned maximum_depth=32;};
struct Measurement {
    original::Identity identity;
    std::uint64_t count=0,probes=0,fhss_nodes=0,fhss_rows=0,fhss_flat_spans=0,fhss_zero_spans=0;
    std::size_t runs=0;
    Failure failure=Failure::none;
    bool valid=false;
};
namespace implementation {
struct InvalidFhss {};
struct InvalidSupport {Failure failure;};
// Exact branch proof using the SAME executed association as gain(0,...):
// raw=RN((sample%two_integer+fraction)+lag), hop=floor(RN(RN(raw*5)/two)),
// d=RN(raw-RN(RN(hop*two)/5)), edge=RN(min(d,RN(RN(two/5)-d))-quiet).
// RN(two/10)==RN(two/5)/2 by binary scaling in this normal finite domain.
inline bool fhss_zero_span(const original::Event& event,SchedHopCoordinate first,SchedHopCoordinate last) {
    if(!event.fhss.enabled||!runtime::supported_fhss(event)||last.distance(first)<0)return false;
    const auto two_integer=2ULL*event.sample_rate;
    if(first.sample/two_integer!=last.sample/two_integer)return false;
    const auto two=2.L*event.sample_rate;
    const auto at=[&](SchedHopCoordinate x) {
        const auto sample=x.sample%two_integer+x.fraction;
        const auto raw=sample+event.fhss.lag_center;
        const auto hop=std::floor(raw*5/two);
        const auto distance=raw-hop*two/5;
        const auto edge=std::min(distance,two/5-distance)-event.fhss.quiet;
        return std::array<long double,4>{raw,hop,distance,edge};
    };
    const auto a=at(first),b=at(last);
    for(const auto x:a)if(!std::isfinite(x))return false;
    for(const auto x:b)if(!std::isfinite(x))return false;
    if(a[0]<0||a[0]>b[0]||b[0]>=two||a[1]!=b[1])return false;
    // Each condition covers one monotone arm, including exact edge==0.
    return (b[2]<=two/10&&b[3]<=0)||(a[2]>=two/10&&a[3]<=0);
}
template<class Emit>
void intersect_fhss(const original::Event& event,sparse::implementation::Engine& engine,
        std::uint64_t first,std::uint64_t end,Limits limits,Measurement& out,
        sparse::Proof& source_work,std::uint64_t& extra,unsigned depth,Emit& append,std::stop_token stop) {
    if(first==end)return;
    const auto available=[&](std::uint64_t n) {
        original::check_stop(stop);const auto source=source_work.probes.total();
        if(source>limits.maximum_probes||extra>limits.maximum_probes-source||n>limits.maximum_probes-source-extra)
            throw sparse::implementation::Exhausted{};
    };
    available(1);++extra;++out.fhss_nodes;
    available(1);const auto left=engine.physical(first);
    available(1);const auto right=engine.physical(end-1);
    if(runtime::fhss_flat_span(event,left,right)){++out.fhss_flat_spans;append(first,end);return;}
    if(fhss_zero_span(event,left,right)){++out.fhss_zero_spans;return;}
    if(end-first<=64) {
        auto run=end;
        for(auto row=first;row<end;++row) {
            available(1);++extra;++out.fhss_rows;
            const auto exact=runtime::evaluate_row(event,engine.piece,row);
            if(!exact.valid())throw InvalidFhss{};
            if(exact.current){if(run==end)run=row;}
            else if(run!=end){append(run,row);run=end;}
        }
        if(run!=end)append(run,end);return;
    }
    if(depth==limits.maximum_depth)throw sparse::implementation::Exhausted{};
    const auto mid=first+(end-first)/2;
    intersect_fhss(event,engine,first,mid,limits,out,source_work,extra,depth+1,append,stop);
    intersect_fhss(event,engine,mid,end,limits,out,source_work,extra,depth+1,append,stop);
}
template<class Emit>
Measurement walk(const original::Event& event,std::span<const original::Piece> pieces,Limits limits,Emit&& emit,std::stop_token stop) {
    Measurement out;out.identity=event.identity;
    const auto fail=[&](Failure f){out.failure=f;out.valid=false;out.count=0;out.runs=0;return out;};
    original::check_stop(stop);if(!runtime::valid_event(event))return fail(Failure::event);
    if(!limits.maximum_runs||!limits.maximum_probes)return fail(Failure::capacity);
    const auto normalized=sparse::piece_contract_event(event);auto cover=event.begin;
    for(const auto& piece:pieces) {
        original::check_stop(stop);
        if(!original::valid(normalized,piece)||piece.first!=cover||piece.end>event.end)return fail(Failure::piece_coverage);
        cover=piece.end;
    }
    if(cover!=event.end)return fail(Failure::piece_coverage);
    sparse::Proof scratch;sparse::Limits engine_limits;engine_limits.max_probes=limits.maximum_probes;
    std::uint64_t previous_end=0,extra_probes=0;bool have=false;
    const auto append=[&](std::uint64_t first,std::uint64_t end) {
        if(first<event.history_first)throw InvalidSupport{Failure::history};
        if(end>event.capture_end)throw InvalidSupport{Failure::capture};
        const auto count=end-first;if(count>UINT64_MAX-out.count)throw sparse::implementation::Exhausted{};
        const bool join=have&&previous_end==first;
        if(!join){if(out.runs==limits.maximum_runs)throw sparse::implementation::Exhausted{};++out.runs;}
        emit(first,end,join);out.count+=count;previous_end=end;have=true;
    };
    try {
        for(const auto& piece:pieces) {
            original::check_stop(stop);if(!piece.location0.active)continue;
            if(extra_probes>=limits.maximum_probes)throw sparse::implementation::Exhausted{};
            engine_limits.max_probes=limits.maximum_probes-extra_probes;
            // Engine::gain is schedule-only even with the ACTUAL FHSS Event;
            // retain source/carry provenance before intersecting the full mask.
            sparse::implementation::Engine engine{event,piece,scratch,engine_limits,stop};
            const auto a=engine.source(piece.first),b=engine.source(piece.end-1);
            if(!std::isfinite(a)||!std::isfinite(b)||a>b||b>=2.L*event.symbol_samples)return fail(Failure::source_coverage);
            if(b>=event.symbol_samples&&(event.index==UINT64_MAX||event.index+1>UINT64_MAX/original::owner_count(event)))return fail(Failure::carry_overflow);
            const auto first=engine.boundary(piece.first,piece.end,engine.source_preimage(0),[&](auto row){return engine.source(row)>=0;});
            const auto end=engine.boundary(first,piece.end,engine.source_preimage(event.symbol_samples),[&](auto row){return engine.source(row)>=event.symbol_samples;});
            if(first==end)continue;
            bool known=false;const auto selected=sparse::implementation::gain_span(engine,first,end,known);
            if(!known)return fail(Failure::gain_unknown);if(selected.first==selected.second)continue;
            if(event.fhss.enabled)intersect_fhss(event,engine,selected.first,selected.second,limits,out,scratch,extra_probes,0,append,stop);
            else append(selected.first,selected.second);
        }
    }catch(const sparse::implementation::Exhausted&){return fail(Failure::capacity);}
    catch(const InvalidFhss&){return fail(Failure::gain_unknown);}
    catch(const InvalidSupport& failure){return fail(failure.failure);}
    out.probes=scratch.probes.total()+extra_probes;original::check_stop(stop);out.valid=true;return out;
}
} // namespace implementation
inline Measurement measure(const original::Event& event,std::span<const original::Piece> pieces,Limits limits={},std::stop_token stop={}) {
    return implementation::walk(event,pieces,limits,[](auto,auto,bool){},stop);
}
// expected comes from the SAME immutable Event/Pieces. The second exact pass
// verifies counts and run count before publication; no per-Piece capacity stays.
inline Selection build(const original::Event& event,std::span<const original::Piece> pieces,const Measurement& expected,
                       std::size_t budget,Limits limits={},std::stop_token stop={}) {
    Selection out;out.identity=event.identity;
    if(!expected.valid||expected.identity!=event.identity){out.failure=expected.failure;return out;}
    if(Selection::reservation(expected.runs)>budget){out.failure=Failure::capacity;return out;}
    out.runs.reserve(expected.runs);
    if(out.working_bytes()>budget){out.failure=Failure::capacity;return out;}
    const auto actual=implementation::walk(event,pieces,limits,[&](auto first,auto end,bool join){
        if(join)out.runs.back().end=end;
        else {if(out.runs.size()==expected.runs)throw Error("Default point measured run count changed");out.runs.push_back({first,end});}
    },stop);
    if(!actual.valid||actual.count!=expected.count||actual.runs!=expected.runs){out.runs.clear();out.failure=actual.valid?Failure::count_overflow:actual.failure;return out;}
    out.count=actual.count;out.probes=actual.probes;out.fhss_nodes=actual.fhss_nodes;out.fhss_rows=actual.fhss_rows;
    out.fhss_flat_spans=actual.fhss_flat_spans;out.fhss_zero_spans=actual.fhss_zero_spans;out.valid=true;return out;
}
inline Selection build(const original::Event& event,std::span<const original::Piece> pieces,
                       std::size_t budget,Limits limits={},std::stop_token stop={}) {
    const auto expected=measure(event,pieces,limits,stop);return build(event,pieces,expected,budget,limits,stop);
}
} // namespace datapump::modem::detail::scheduled_default_point_observation
