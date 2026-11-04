#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <string_view>
#include <type_traits>

namespace datapump::modem::detail::scheduled_certificate_diagnostics {
// Private opt-in accounting only. No sample, key, lane map or scoring capability
// is retained. IEEE classification uses integer bits and never changes fenv.
inline bool requested() noexcept {
    const auto* value=std::getenv("DATAPUMP_SCHED_CERTIFICATE_DIAGNOSTICS");
    return value && std::string_view(value)=="1";
}
enum class Cohort : unsigned {fp32_exact,fp32_proxy,fp64_exact,fp64_proxy,unavailable,count};
inline constexpr std::size_t cohorts=static_cast<std::size_t>(Cohort::count);
inline constexpr std::array<const char*,cohorts> cohort_names{"fp32_exact","fp32_proxy","fp64_exact","fp64_proxy","unavailable"};
template<class T> constexpr Cohort cohort(bool proxy) noexcept {
    if constexpr(std::is_same_v<T,float>)return proxy?Cohort::fp32_proxy:Cohort::fp32_exact;
    else return proxy?Cohort::fp64_proxy:Cohort::fp64_exact;
}
inline void add(std::uint64_t& target,std::uint64_t increment,bool& saturated) noexcept {
    if(increment>std::numeric_limits<std::uint64_t>::max()-target){target=std::numeric_limits<std::uint64_t>::max();saturated=true;}
    else target+=increment;
}
inline constexpr std::size_t magnitude_bins=22,score_bins=12;
inline std::size_t magnitude_bin(double value) noexcept {
    const auto bits=std::bit_cast<std::uint64_t>(value);const std::uint64_t absolute=bits&UINT64_C(0x7fffffffffffffff);
    const auto exponent=(absolute>>52)&0x7ffU;
    if(exponent==0x7ffU)return (absolute&0x000fffffffffffffULL)?3:2;
    if(!absolute)return 1;
    if(bits>>63)return 0;
    const auto power=static_cast<int>(exponent)-1023;
    if(power<-64)return 4;
    if(power>=64)return 21;
    return 5+static_cast<std::size_t>((power+64)/8);
}
inline std::size_t score_bin(double value) noexcept {
    const auto bits=std::bit_cast<std::uint64_t>(value);const std::uint64_t absolute=bits&UINT64_C(0x7fffffffffffffff);
    if((absolute>>52)==0x7ffU)return (absolute&0x000fffffffffffffULL)?11:10;
    if(!absolute)return 1;
    if(bits>>63)return 0;
    constexpr std::array<double,7> ends{1,4,16,32,64,128,512};
    for(std::size_t i=0;i<ends.size();++i)if(absolute<std::bit_cast<std::uint64_t>(ends[i]))return i+2;
    return 9;
}
inline bool finite_nonnegative(double value) noexcept {
    const auto bits=std::bit_cast<std::uint64_t>(value);
    return !(bits>>63) && ((bits>>52)&0x7ffU)!=0x7ffU;
}
inline bool less_nonnegative(double a,double b) noexcept {
    return finite_nonnegative(a) && finite_nonnegative(b) && std::bit_cast<std::uint64_t>(a)<std::bit_cast<std::uint64_t>(b);
}
// Diagnostic classification of an already computed guard operand. This does
// not repeat a floating predicate that the original short circuit may omit.
inline bool at_or_below_positive_floor(double value,double positive_floor) noexcept {
    const auto bits=std::bit_cast<std::uint64_t>(value);const std::uint64_t absolute=bits&UINT64_C(0x7fffffffffffffff);
    if((absolute>>52)==0x7ffU && (absolute&0x000fffffffffffffULL))return false;
    return !absolute || (bits>>63) || absolute<=std::bit_cast<std::uint64_t>(positive_floor);
}
template<std::size_t N> struct Bins {
    std::array<std::uint64_t,N> counts{};
    bool saturated=false;
    void observe(std::size_t bin) noexcept {add(counts[bin],1,saturated);}
    void merge(const Bins& other) noexcept {
        for(std::size_t i=0;i<N;++i)add(counts[i],other.counts[i],saturated);
        saturated|=other.saturated;
    }
};
struct Histogram : Bins<magnitude_bins> {void observe(double value) noexcept {Bins<magnitude_bins>::observe(magnitude_bin(value));}};
template<std::size_t N> struct ArrayView {const std::array<std::uint64_t,N>& values;};
template<std::size_t N> std::ostream& operator<<(std::ostream& out,ArrayView<N> view) {
    out<<'[';for(std::size_t i=0;i<N;++i){if(i)out<<',';out<<view.values[i];}return out<<']';
}
template<std::size_t N> ArrayView<N> view(const Bins<N>& bins) noexcept {return {bins.counts};}
enum class Failure : unsigned {none,no_screen,carrier_nonfinite,count_overflow,invalid_contract,
    nonfinite_errors,nonpositive_gram,energy_floor,fraction_not_below_one,proxy_invalid,score_overflow,count};
inline constexpr std::size_t failures=static_cast<std::size_t>(Failure::count);
struct ScoreDetail {
    Failure failure=Failure::invalid_contract;
    double determinant_lower=0,energy_lower=0,fraction=0;
    bool has_gram=false,has_energy=false,has_fraction=false;
};
struct ProxyDetail {
    double radius=0,dot_l1=0,dot_l2=0,gram_l1=0,gram_l2=0;
    bool available=false;
};
// One fixed aggregate inside each Accumulator; sizeof accounting covers it.
struct NativeProfile {
    Histogram lambda;
    std::uint64_t candidates=0,requires_refinement=0,valid_certificates=0;
    bool saturated=false;
    void merge(const NativeProfile& other) noexcept {
        lambda.merge(other.lambda);add(candidates,other.candidates,saturated);
        add(requires_refinement,other.requires_refinement,saturated);add(valid_certificates,other.valid_certificates,saturated);saturated|=other.saturated;
    }
};
struct PreparationCohort {
    Histogram displacement,l1,gram,l2_squared;
    NativeProfile native;
    std::uint64_t ranges=0;bool saturated=false;
    void merge(const PreparationCohort& other) noexcept {
        displacement.merge(other.displacement);l1.merge(other.l1);gram.merge(other.gram);l2_squared.merge(other.l2_squared);native.merge(other.native);
        add(ranges,other.ranges,saturated);saturated|=other.saturated;
    }
};
struct Preparation {
    std::array<PreparationCohort,cohorts> cohort{};
    void observe(Cohort tag,double displacement,double l1,double gram,double square,const NativeProfile& native) noexcept {
        auto& value=cohort[static_cast<std::size_t>(tag)];add(value.ranges,1,value.saturated);
        value.displacement.observe(displacement);value.l1.observe(l1);value.gram.observe(gram);value.l2_squared.observe(square);value.native.merge(native);
    }
    void merge(const Preparation& other) noexcept {for(std::size_t i=0;i<cohorts;++i)cohort[i].merge(other.cohort[i]);}
};
enum class Metric : unsigned {cert_dot,cert_gram,cert_energy_lower,cert_energy_upper,reference_dot,reference_gram,reference_energy,
    proxy_radius,proxy_dot_l1,proxy_dot_l2,proxy_gram_l1,proxy_gram_l2,carrier_radius,mask_energy_debit,
    determinant_lower,effective_energy_lower,explained_fraction,cold_upper,count};
inline constexpr std::size_t metrics=static_cast<std::size_t>(Metric::count);
inline constexpr std::array<const char*,metrics> metric_names{"cert_dot","cert_gram","cert_energy_lower","cert_energy_upper",
    "reference_dot","reference_gram","reference_energy","proxy_l2_radius","proxy_dot_l1","proxy_dot_l2","proxy_gram_l1","proxy_gram_l2",
    "carrier_radius","mask_energy_debit","determinant_lower","effective_energy_lower","explained_fraction","cold_upper"};
struct ColdCohort {
    std::array<Histogram,metrics> metric{};
    Bins<failures> failure;
    std::uint64_t attempts=0,rejections=0,valid_certificates=0,valid_references=0;
    bool saturated=false;
    void observe(Metric name,double value) noexcept {metric[static_cast<std::size_t>(name)].observe(value);}
};
struct Pairs {
    Bins<score_bins> upper,raw,guarded;
    Bins<score_bins*score_bins> upper_raw,upper_guarded;
    std::uint64_t completed=0,raw_exceeds_upper=0,raw_below_threshold=0,guarded_below_threshold=0;
    std::uint64_t guarded_bound_pairs=0,guarded_exceeds_upper=0;
    bool saturated=false;
};
struct Probe {
    Cohort cohort=Cohort::unavailable;
    double upper=std::numeric_limits<double>::infinity(),threshold=0,raw=0,guarded=0;
    std::uint64_t lane=0,index=0;
    bool original_scored=false;
    double raw_upper=std::numeric_limits<double>::infinity();
    bool guarded_bound=false;
};
struct State {
    const bool enabled=requested();
    Preparation preparation;
    std::array<ColdCohort,cohorts> cold{};
    std::array<Pairs,cohorts> pairs{};
    std::uint64_t resident_completions=0,pair_identity_mismatches=0;bool saturated=false;
    ColdCohort& at(Cohort tag) noexcept {return cold[static_cast<std::size_t>(tag)];}
    void completed(const Probe& probe) noexcept {
        if(!enabled || !probe.original_scored)return;
        auto& value=pairs[static_cast<std::size_t>(probe.cohort)];const auto u=score_bin(probe.upper),r=score_bin(probe.raw),g=score_bin(probe.guarded);
        value.upper.observe(u);value.raw.observe(r);value.guarded.observe(g);value.upper_raw.observe(u*score_bins+r);value.upper_guarded.observe(u*score_bins+g);
        add(value.completed,1,value.saturated);
        if(less_nonnegative(probe.guarded_bound?probe.raw_upper:probe.upper,probe.raw))add(value.raw_exceeds_upper,1,value.saturated);
        if(probe.guarded_bound)add(value.guarded_bound_pairs,1,value.saturated);
        if(less_nonnegative(probe.upper,probe.guarded))add(value.guarded_exceeds_upper,1,value.saturated);
        if(less_nonnegative(probe.raw,probe.threshold))add(value.raw_below_threshold,1,value.saturated);
        if(less_nonnegative(probe.guarded,probe.threshold))add(value.guarded_below_threshold,1,value.saturated);
    }
};
// Twenty digits per uint64 plus separators. Each separately emitted record has
// a fixed worst-case bound below the trace's 16384-byte record ceiling.
inline constexpr std::size_t cold_record_bound=2048+metrics*(64+magnitude_bins*21)+failures*21;
inline constexpr std::size_t pair_record_bound=2048+(2*score_bins*score_bins+3*score_bins)*21;
static_assert(cold_record_bound<16384 && pair_record_bound<16384);
} // namespace datapump::modem::detail::scheduled_certificate_diagnostics
