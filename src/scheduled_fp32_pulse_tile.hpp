#pragma once
// Output-only diagnostic. No production arithmetic selection is changed.
#include "scheduled_ordered_pulse_candidates.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace datapump::modem::detail {
template<std::size_t Samples=512,std::size_t Coefficients=96>
class ScheduledFp32PulseTile {
public:
    using Original=ScheduledOrderedPulseCandidatesT<1,Samples,Coefficients>;
    using Plan=typename Original::Plan;
    using Lane=typename Original::Lane;
    using Value=std::array<std::complex<double>,2>;
    using Components=std::array<double,4>;
    using CoefficientInput=std::array<Components,Coefficients>;
    struct Statistics {
        std::uint64_t plan_packs=0,weight_narrowings=0,prepares=0,value_pairs=0;
        std::uint64_t coefficient_narrowings=0,native_samples=0,error_fallback_samples=0;
        std::uint64_t range_fallbacks=0,environment_fallbacks=0,shape_terms=0,output_widenings=0;
    } statistics;
    struct Accuracy {double maximum_component_error;constexpr Accuracy(double maximum=1e-5):maximum_component_error(maximum){}};
    struct PackedRow {
        std::array<float,18> weight{};
        double absolute_weight_sum=0; // outward, in normalized weight units
        std::uint32_t nonzero=0;
        std::uint16_t first_offset=0,taps=0;
    };
    // Public-only optional quantized Plan. It owns float weights and exact
    // control metadata copied under the original immutable Plan lease. The
    // original lease is needed only while pack() reads it. Root owns exact
    // content/cache-key identity; no pointer/hash aliasing is a proof here.
    struct PackedPlan {
        std::array<PackedRow,Samples> rows{};
        std::bitset<Coefficients> active_taps;
        std::size_t samples=0,coefficients=0;
        std::uint64_t first_local=0,symbol_samples=0,chip_samples=0;
        double scale=1,maximum_weight_sum=0;
        std::uint64_t shape_terms=0;
        bool valid=false;
        template<class SourcePlan>
        bool pack(const SourcePlan& source,Statistics& stats,std::stop_token stop={}) {
            valid=false;samples=coefficients=0;active_taps.reset();maximum_weight_sum=0;shape_terms=0;
            if(!supported() || !source.well_formed())return false;
            double peak=0;
            for(std::size_t n=0;n<source.samples;++n)for(std::size_t p=0;p<source.rows[n].taps;++p)
                peak=std::max(peak,std::abs(source.rows[n].weight[p]));
            scale=power_scale(peak);
            if(!std::isfinite(scale) || scale<=0){++stats.range_fallbacks;return false;}
            for(std::size_t n=0;n<source.samples;++n) {
                if((n&31U)==0 && stop.stop_requested())throw Error("FP32 pulse packing cancelled");
                auto& out=rows[n];out={};const auto& in=source.rows[n];
                if(in.first_offset>65535 || in.taps>18)return false;
                out.first_offset=static_cast<std::uint16_t>(in.first_offset);
                out.taps=static_cast<std::uint16_t>(in.taps);out.nonzero=in.nonzero;
                double sum=0;
                for(std::size_t p=0;p<in.taps;++p)if(in.nonzero&(std::uint32_t{1}<<p)) {
                    const double normalized=in.weight[p]/scale;
                    // Active source weights are nonzero. Reject scaling to
                    // zero so every accepted dyadic normalization is exact
                    // and float-normal; retain the original FP64 refinement.
                    if(normalized==0 || !narrow(normalized,out.weight[p])){++stats.range_fallbacks;return false;}
                    sum+=std::abs(normalized);++stats.weight_narrowings;
                }
                // <=18 nonnegative terms: ordinary sum has <=17 rounded
                // additions. The computed factor is 1+64u, leaving >46u
                // margin even after final multiply rounding. This inflation
                // itself bounds upward under RN; zero rows stay exactly zero.
                // No nextafter/libm call is needed anywhere in the L1 loop.
                out.absolute_weight_sum=sum*(1+gamma(64,double_unit));
                maximum_weight_sum=std::max(maximum_weight_sum,out.absolute_weight_sum);
                // source.well_formed() above certifies this derived count;
                // nonzero was copied unchanged. Keep that untrusted API check.
                shape_terms+=in.nonzero_count;
            }
            samples=source.samples;coefficients=source.coefficients;active_taps=source.active_taps;
            first_local=source.first_local;symbol_samples=source.symbol_samples;chip_samples=source.chip_samples;
            valid=true;++stats.plan_packs;return true;
        }
    };
    // Explicit build contract; original FP64 primitive remains unchanged.
    static bool supported() {
        return Original::arithmetic_supported() && std::numeric_limits<float>::is_iec559 &&
            std::numeric_limits<float>::digits==24;
    }
    void reset(){prepared_=evaluated_=false;plan_=nullptr;evaluated_samples_=0;}
    // Low-level shared interface with integer diagnostic: original union is
    // coefficient[tap][bit0.real,bit0.imag,bit1.real,bit1.imag]. Only active
    // taps are inspected; padded slots never create new private reads.
    bool prepare(const PackedPlan& plan,const CoefficientInput& original,Accuracy accuracy={},
                 std::stop_token stop={}) {
        reset();
        if(!supported()){++statistics.environment_fallbacks;return false;}
        if(!plan.valid || !std::isfinite(accuracy.maximum_component_error) ||
           accuracy.maximum_component_error<0)return false;
        const auto& source=plan;
        double peak=0;
        for(std::size_t p=0;p<source.coefficients;++p)if(source.active_taps.test(p))
            for(const auto x:original[p]) {
                if(!std::isfinite(x)){++statistics.range_fallbacks;return false;}
                peak=std::max(peak,std::abs(x));
            }
        coefficient_scale_=power_scale(peak);units_=coefficient_scale_*plan.scale;
        if(!std::isfinite(units_) || units_<std::numeric_limits<double>::min() ||
           units_>std::numeric_limits<double>::max()/64) {++statistics.range_fallbacks;return false;}
        peak_normalized_=peak/coefficient_scale_;
        // One conservative tile-wide certificate, BEFORE all output arithmetic.
        // Row L1 is prepared with the public weights; no per-output polynomial,
        // nextafter, sqrt or FP64 refinement runs on an accepted native tile.
        const double polynomial=up(plan.maximum_weight_sum*peak_normalized_);
        radius_=plan.shape_terms?up(units_*(gamma(24,float_unit)*polynomial+
            128*std::numeric_limits<float>::denorm_min()+gamma(64,double_unit)*polynomial)+
            8*std::numeric_limits<double>::denorm_min()):0;
        if(!std::isfinite(radius_) || radius_>accuracy.maximum_component_error) {
            statistics.error_fallback_samples+=plan.samples;return false;
        }
        for(std::size_t p=0;p<source.coefficients;++p)if(source.active_taps.test(p)) {
            if((p&7U)==0 && stop.stop_requested())throw Error("FP32 coefficient packing cancelled");
            for(unsigned component=0;component<4;++component) {
                const double x=original[p][component]/coefficient_scale_;
                if(!narrow(x,coefficients_[p][component])){++statistics.range_fallbacks;return false;}
                ++statistics.coefficient_narrowings;
            }
        }
        plan_=&plan;prepared_=true;return true;
    }
    // PatternCode gathering is deliberately ORIGINAL private values(), not a
    // public sign/conjugacy derivation. Caller restores original lane context
    // after any attempt, including failure/exception; no mutation rollback is
    // claimed. The optional coefficient input is diagnostic scratch, charged
    // by caller; it may be reused by integer/FP64 comparison outside timing.
    static bool gather(const PackedPlan& plan,const Lane& lane,CoefficientInput& result,
                       Statistics& stats,std::stop_token stop={}) {
        if(!plan.valid || !lane.code || !lane.original_config ||
           lane.original_config->spreading_mode!=SpreadingMode::pattern ||
           !pattern_pulse_enabled(*lane.original_config) ||
           lane.code->chip_samples()!=plan.chip_samples ||
           lane.code->symbol_samples()!=plan.symbol_samples)return false;
        const auto& source=plan;
        for(std::size_t p=0;p<source.coefficients;++p)if(source.active_taps.test(p)) {
            if(p>std::numeric_limits<std::uint64_t>::max()-lane.first_local)return false;
            const auto local=lane.first_local+p;
            if(local>=lane.code->chips_per_symbol() || local>std::numeric_limits<std::uint64_t>::max()-lane.first_chip)return false;
        }
        if(stop.stop_requested())throw Error("FP32 private gather cancelled");
        lane.activate(stop);++stats.prepares;
        for(std::size_t p=0;p<source.coefficients;++p)if(source.active_taps.test(p)) {
            if((p&7U)==0 && stop.stop_requested())throw Error("FP32 private gather cancelled");
            const auto value=lane.code->values(lane.first_chip+lane.first_local+p,0);++stats.value_pairs;
            result[p]={value[0].real(),value[0].imag(),value[1].real(),value[1].imag()};
        }
        return true;
    }
#if defined(__SSE2__)
    // A vector-return leaf keeps the recurrence separate from std::array's
    // aggregate return ABI. No tap is reassociated or accumulated separately.
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static __m128 contract_native_sse2(const PackedRow& row,
            const std::array<std::array<float,4>,Coefficients>& coefficient) {
        auto sum=_mm_setzero_ps();
        for(std::size_t p=0;p<row.taps;++p)if(row.nonzero&(std::uint32_t{1}<<p)) {
            const auto product=_mm_mul_ps(_mm_set1_ps(row.weight[p]),
                _mm_loadu_ps(coefficient[row.first_offset+p].data()));
            sum=_mm_add_ps(sum,product);
        }
        return sum;
    }
#endif
    // Four independent components, no horizontal reduction, no conversion or
    // scale operation inside the ordered tap loop; no FMA is permitted.
    static std::array<float,4> contract_native(const PackedRow& row,
            const std::array<std::array<float,4>,Coefficients>& coefficient) {
#if defined(__SSE2__)
        const auto sum=contract_native_sse2(row,coefficient);
        std::array<float,4> result;_mm_storeu_ps(result.data(),sum);return result;
#else
        std::array<float,4> result{};
        for(std::size_t p=0;p<row.taps;++p)if(row.nonzero&(std::uint32_t{1}<<p))
            for(unsigned c=0;c<4;++c) {
                const float product=row.weight[p]*coefficient[row.first_offset+p][c];result[c]+=product;
            }
        return result;
#endif
    }
    bool evaluate(std::stop_token stop={}) {
        evaluated_=false;
        if(!prepared_ || !supported()){++statistics.environment_fallbacks;return false;}
        const auto& source=*plan_;
        for(std::size_t n=0;n<source.samples;++n) {
            if((n&31U)==0 && stop.stop_requested())throw Error("FP32 pulse contraction cancelled");
            output_[n]=contract_native(plan_->rows[n],coefficients_);
        }
        statistics.native_samples+=source.samples;statistics.shape_terms+=source.shape_terms;
        evaluated_samples_=source.samples;evaluated_=true;return true;
    }
    // Output/radius are owned. Drop the PUBLIC packed-plan lease immediately
    // after evaluate(), before original ordered sample/outer-ring callbacks.
    void detach_evaluated_plan(){plan_=nullptr;prepared_=false;}
    bool raw(std::size_t sample,Value& result,double* component_radius=nullptr) {
        if(!evaluated_ || sample>=evaluated_samples_)return false;
        const auto& f=output_[sample];
        result={{{units_*static_cast<double>(f[0]),units_*static_cast<double>(f[1])},
                 {units_*static_cast<double>(f[2]),units_*static_cast<double>(f[3])}}};
        statistics.output_widenings+=4;if(component_radius)*component_radius=radius_;return true;
    }
    template<class SourcePlan>
    static Value ordered_reference(const SourcePlan& plan,const CoefficientInput& coefficient,std::size_t sample) {
        const auto& row=plan.rows[sample];Components sum{};
        for(std::size_t p=0;p<row.taps;++p)if(row.nonzero&(std::uint32_t{1}<<p))
            for(unsigned c=0;c<4;++c) {const double product=row.weight[p]*coefficient[row.first_offset+p][c];sum[c]+=product;}
        return {{{sum[0],sum[1]},{sum[2],sum[3]}}};
    }
    double radius(std::size_t)const{return radius_;}
    // Only algebra/template bound; radial projection is nonexpansive in exact
    // arithmetic. Original FP64 limiter rounding and full Fit/score sensitivity
    // are left to the oracle. This never authorizes a detector decision.
    double exact_limited_complex_radius(std::size_t,double gain)const {
        return up(std::abs(gain)*std::sqrt(2.)*radius_);
    }
private:
    static constexpr double float_unit=0x1p-24,double_unit=0x1p-53;
    static double gamma(unsigned n,double u){return (n*u)/(1-n*u);}
    static double up(double x){return std::nextafter(x,std::numeric_limits<double>::infinity());}
    static double power_scale(double peak) {int exponent=0;if(peak)std::frexp(peak,&exponent);return peak?std::ldexp(1.,exponent):1.;}
    static bool narrow(double x,float& result) {
        if(!std::isfinite(x) || (x!=0 && std::abs(x)<std::numeric_limits<float>::min()) ||
           std::abs(x)>std::numeric_limits<float>::max())return false;
        result=static_cast<float>(x);return true;
    }
    alignas(64)std::array<std::array<float,4>,Coefficients> coefficients_{};
    alignas(64)std::array<std::array<float,4>,Samples> output_{};
    double radius_=0;
    const PackedPlan* plan_=nullptr;
    double coefficient_scale_=1,units_=1,peak_normalized_=0;
    std::size_t evaluated_samples_=0;
    bool prepared_=false,evaluated_=false;
};
} // namespace datapump::modem::detail
