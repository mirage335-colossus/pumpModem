#pragma once
#include "datapump/sched_hop.hpp"
#include "datapump/fhss_schedule.hpp"
#include <atomic>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace datapump::modem::detail {
struct ScheduledFlatExportCounts {std::uint64_t rows=0,samples=0,raw_pieces=0,raw_samples=0;};
inline std::atomic<std::uint64_t> scheduled_flat_export_rows{0},scheduled_flat_export_samples{0};
inline std::atomic<std::uint64_t> scheduled_flat_raw_pieces{0},scheduled_flat_raw_samples{0};
inline void scheduled_flat_export_note(std::uint64_t samples)noexcept {
    scheduled_flat_export_rows.fetch_add(1,std::memory_order_relaxed);scheduled_flat_export_samples.fetch_add(samples,std::memory_order_relaxed);
}
inline void scheduled_flat_raw_note(std::uint64_t samples)noexcept {
    scheduled_flat_raw_pieces.fetch_add(1,std::memory_order_relaxed);scheduled_flat_raw_samples.fetch_add(samples,std::memory_order_relaxed);
}
inline ScheduledFlatExportCounts scheduled_flat_export_counts()noexcept {
    return {scheduled_flat_export_rows.load(std::memory_order_relaxed),scheduled_flat_export_samples.load(std::memory_order_relaxed),
        scheduled_flat_raw_pieces.load(std::memory_order_relaxed),scheduled_flat_raw_samples.load(std::memory_order_relaxed)};
}
inline bool scheduled_flat_export_requested()noexcept {
    static const bool value=[] {const auto* v=std::getenv("DATAPUMP_SCHED_FLAT_EXPORT");return v && std::strcmp(v,"1")==0;}();
    return value;
}
class ScheduledFlatExportCertificate;
ScheduledFlatExportCertificate scheduled_flat_export_certificate(const void*,long double,long double,
    std::uint64_t,std::uint64_t,std::uint64_t,SchedHopCoordinate,SchedHopCoordinate,
    const SchedHopLocation&,const FhssPowerShape&,std::uint32_t,bool);
// Local synchronous capability, never retained in a cache or across jobs. It
// represents a tightened ALREADY SUCCESSFUL original phase-family flat proof.
class ScheduledFlatExportCertificate {
    friend ScheduledFlatExportCertificate scheduled_flat_export_certificate(const void*,long double,long double,
        std::uint64_t,std::uint64_t,std::uint64_t,SchedHopCoordinate,SchedHopCoordinate,
        const SchedHopLocation&,const FhssPowerShape&,std::uint32_t,bool);
    const void* owner_=nullptr;
    long double origin_=0,rate_=0;
    std::uint64_t phase_=0,first_=0,end_=0;
public:
    bool valid()const noexcept{return owner_!=nullptr;}
    bool covers(const void* owner,long double origin,long double rate,std::uint64_t phase,
                std::uint64_t first,std::uint64_t end)const noexcept {
        return owner_==owner && owner_ && origin_==origin && rate_==rate && phase_==phase &&
            first>=first_ && end<=end_ && first<end;
    }
};
inline ScheduledFlatExportCertificate scheduled_flat_export_certificate(const void* owner,
        long double origin,long double rate,std::uint64_t phase,std::uint64_t first,std::uint64_t end,
        SchedHopCoordinate left,SchedHopCoordinate right,const SchedHopLocation& group,
        const FhssPowerShape& fhss,std::uint32_t sample_rate,bool original_full_flat) {
    ScheduledFlatExportCertificate result;
    // This shortcut is optional even for a valid original phase family. Runtime
    // fenv checks alone cannot establish the compiler's expression policy.
#if !defined(DATAPUMP_ORDERED_PULSE_CONTRACT_OFF) || defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0)
    return result;
#endif
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 || FLT_EVAL_METHOD!=0 ||
       !original_full_flat || !owner || !sample_rate || sample_rate%5 ||
       std::numeric_limits<long double>::digits<53 || std::fegetround()!=FE_TONEAREST ||
       !std::isfinite(origin) || std::abs(origin)>=0x1p31L || !std::isfinite(rate) || rate<.5L || rate>2.L ||
       first>=end || end>=(std::uint64_t{1}<<31) || !group.active ||
       left.distance({group.flat_begin,0})<2 || right.distance({group.flat_end,0})> -2)return result;
    // Local tick subtraction/multiplication magnitudes are <2^33. At precision
    // >=53, a deliberately loose64*epsilon*2^33 error bound is <=2^-13sample
    // for global-tick versus original piece-reanchored tick evaluation. FHSS
    // modular intermediates below can approach2^34; even a loose64*epsilon*2^35
    // bound remains <=2^-11sample. Both are far below the strict two-sample
    // interior margin, preserving literal gain1. UTC integers themselves are
    // NEVER converted to floating time. This bounds only gain-cell membership;
    // ORIGINAL source arithmetic, reanchors, carrier phase and ownership stay.
    if(fhss.enabled && fhss.smooth) {
        const auto two=2.L*sample_rate;
        if(two>=0x1p31L || !std::isfinite(fhss.lag_center) || std::abs(fhss.lag_center)>=0x1p31L ||
           !std::isfinite(fhss.support) || fhss.support<1 || !std::isfinite(fhss.quiet))return result;
        const auto argument=static_cast<long double>(left.sample%(2ULL*sample_rate))+left.fraction+fhss.lag_center;
        if(!std::isfinite(argument) || std::abs(argument)>=0x1p31L)return result;
        auto local=std::fmod(argument,two);if(local<0)local+=two;
        const auto period=two/5;
        const auto position=local-std::floor(local*5/two)*period;
        const auto radius=fhss.affected_radius();
        if(position<radius+2 || position+right.distance(left)>period-radius-2)return result;
    }
    result.owner_=owner;result.origin_=origin;result.rate_=rate;result.phase_=phase;
    result.first_=first;result.end_=end;return result;
}
} // namespace datapump::modem::detail
