#pragma once
#include "datapump/sched_hop.hpp"
#include <atomic>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace datapump::modem::detail {
struct ScheduledR1ExportCounts {std::uint64_t attempts=0,rows=0,samples=0;};
inline std::atomic<std::uint64_t> scheduled_r1_export_attempts{0},scheduled_r1_export_rows{0},scheduled_r1_export_samples{0};
inline void scheduled_r1_export_note_attempt()noexcept{scheduled_r1_export_attempts.fetch_add(1,std::memory_order_relaxed);}
inline void scheduled_r1_export_note_row(std::uint64_t samples)noexcept {
    scheduled_r1_export_rows.fetch_add(1,std::memory_order_relaxed);scheduled_r1_export_samples.fetch_add(samples,std::memory_order_relaxed);
}
inline ScheduledR1ExportCounts scheduled_r1_export_counts()noexcept {
    return {scheduled_r1_export_attempts.load(std::memory_order_relaxed),scheduled_r1_export_rows.load(std::memory_order_relaxed),scheduled_r1_export_samples.load(std::memory_order_relaxed)};
}
// Diagnostic until complete paired execution chooses its automatic crossover.
inline bool scheduled_r1_export_requested()noexcept {
    static const bool enabled=[] {const auto* v=std::getenv("DATAPUMP_SCHED_R1_EXPORT");return v && std::strcmp(v,"1")==0;}();
    return enabled;
}
inline bool scheduled_r1_export_grid(long double value)noexcept {
    if(!std::isfinite(value) || std::abs(value)>=0x1p31L)return false;
    const auto scaled=std::ldexp(value,21);
    return std::trunc(scaled)==scaled;
}
struct ScheduledR1ExportInput {
    long double rate=0,origin=0,symbol_source=0;
    std::uint64_t block=0,piece_first=0,selected_first=0,end=0,symbol=0,chip=0;
    std::uint64_t current_index=0,target_index=0;
    SchedHopCoordinate first_source{},source_anchor{};
    bool cuts_aligned=false,full_flat=false;
};
struct ScheduledR1ExportProof {bool eligible=false;double first=0;};
// Full-flat is supplied ONLY by the original whole-range scheduled_flat(...,
// false) proof, covering BOTH keyed Sched_Hop and FHSS. No representative-row
// null proof is used. UTC/source integers remain integral; only bounded local
// DIFFERENCES participate in the exact dyadic arithmetic argument.
inline ScheduledR1ExportProof scheduled_r1_export_proof(const ScheduledR1ExportInput& x) {
    // Keep this diagnostic unavailable when compiler reassociation/contraction
    // policy or the binary64 evaluation contract has not been qualified.
#if !defined(DATAPUMP_ORDERED_PULSE_CONTRACT_OFF) || defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0)
    return {};
#endif
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 ||
       FLT_EVAL_METHOD!=0 || std::fegetround()!=FE_TONEAREST ||
       x.rate!=1 || !x.full_flat || !x.cuts_aligned || !x.chip || x.chip>1023 ||
       !x.symbol || x.symbol>=(std::uint64_t{1}<<31) ||
       x.selected_first<x.block || x.selected_first<x.piece_first || x.end<=x.selected_first ||
       x.end-x.block>512 || x.end>=(std::uint64_t{1}<<31) ||
       !scheduled_r1_export_grid(x.origin) || !scheduled_r1_export_grid(x.symbol_source) ||
       !scheduled_r1_export_grid(x.first_source.fraction) || !scheduled_r1_export_grid(x.source_anchor.fraction))return {};
    const auto relative=x.first_source.distance(x.source_anchor);
    if(!scheduled_r1_export_grid(relative))return {};
    const auto gross=relative-x.symbol_source;
    if(!scheduled_r1_export_grid(gross) || gross<0 || gross>=2.L*x.symbol)return {};
    const bool next=gross>=x.symbol;
    if(next && x.current_index==std::numeric_limits<std::uint64_t>::max())return {};
    if(x.target_index!=x.current_index+(next?1U:0U))return {};
    const auto first=next?gross-x.symbol:gross;
    const auto last=first+static_cast<long double>(x.end-x.selected_first-1);
    if(!scheduled_r1_export_grid(first) || !scheduled_r1_export_grid(last) || last>=x.symbol)return {};
    // <=31 integral +21 fractional bits fit binary64. Every original unit-rate
    // source add/subtract/cast (including chip/quarter reanchors) is exact when
    // these bounds and the caller's original control grid hold. Keep a direct
    // round-trip check as an independent eligibility assertion.
    const auto value=static_cast<double>(first);
    if(static_cast<long double>(value)!=first)return {};
    return {true,value};
}
struct ScheduledR1ExportResult {std::uint64_t end=0,appended=0;};
template<class Output,class Skip,class Cancel>
ScheduledR1ExportResult scheduled_r1_export_append(Output& output,const ScheduledR1ExportProof& proof,
        std::uint64_t block,std::uint64_t selected_first,std::uint64_t end,Skip&& skip,Cancel&& cancel) {
    ScheduledR1ExportResult result{selected_first,0};
    if(!proof.eligible)return result;
    for(;result.end<end;++result.end) {
        if((result.end&31U)==0)cancel();
        const auto offset=static_cast<std::size_t>(result.end-block);
        if(skip(offset))continue;
        const auto at=proof.first+static_cast<double>(result.end-selected_first);
        if(!output.append(offset,at))break;
        ++result.appended;
    }
    return result;
}
} // namespace datapump::modem::detail
