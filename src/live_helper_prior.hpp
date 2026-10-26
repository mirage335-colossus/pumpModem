#pragma once

#include "datapump/pattern_search.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
#include <xmmintrin.h>
#endif

namespace datapump::live::detail {

// Only original catalogue cells are selected. Measurements narrow optional
// acquisition; they never remove an original clock lane or choose payload END.
struct HelperPriorFacts {
    std::size_t selected=0,measured_selected=0,rates=0,represented_rates=0;
    double origin_hz=0,radius_hz=0,clock_cell_half_width_ppm=0;
    const char* reason="ordinary receiver has no supported native oscillator catalogue; broad acquisition retained";
    bool admitted=false;
};
struct HelperPriorMask : HelperPriorFacts {
    std::array<std::uint8_t,modem::maximum_pattern_frequency_rate_hypotheses> cells{};
};

namespace helper_prior_detail {
inline bool environment() {
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0) || (defined(_M_FP_FAST) && _M_FP_FAST) || FLT_EVAL_METHOD!=0
    return false;
#else
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::radix!=2 ||
       std::numeric_limits<double>::digits!=53 || std::numeric_limits<double>::max_exponent!=1024 ||
       sizeof(double)!=sizeof(std::uint64_t) || std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    if(_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))return false;
#endif
    // Check BOTH subnormal input and subnormal output behavior. Bit checks
    // cannot mistake a DAZ comparison of a flushed value for equality.
    volatile double tiny=std::numeric_limits<double>::denorm_min(),one=1;
    volatile double normal=std::numeric_limits<double>::min(),half=.5;
    const double input=tiny*one,output=normal*half;
    return std::bit_cast<std::uint64_t>(input)==1 &&
        std::bit_cast<std::uint64_t>(output)==0x0008000000000000ULL;
#endif
}
struct Interval {double lower=0,upper=0;};
inline bool finite(Interval value) {return std::isfinite(value.lower)&&std::isfinite(value.upper);}
// Error-free two-sum preserves exact touching clock endpoints. Only inexact
// endpoints are directed inward/outward; an epsilon never joins a real gap.
// The caller requires IEC559 round-to-nearest arithmetic before using this.
inline Interval sum(double a,double b) {
    const auto value=a+b;
    if(!std::isfinite(value))return {value,value};
    const auto bv=value-a;
    const auto error=(a-(value-bv))+(b-bv);
    const auto infinity=std::numeric_limits<double>::infinity();
    return {error<0?std::nextafter(value,-infinity):value,
            error>0?std::nextafter(value,infinity):value};
}
inline Interval product(double a,double b) {
    if(a==0 || b==0)return {};
    const auto value=a*b,infinity=std::numeric_limits<double>::infinity();
    // One outward ulp also encloses subnormal/underflow rounding. No exact
    // product claim is needed, so this does not rely on a rounded FMA residual.
    return {std::nextafter(value,-infinity),std::nextafter(value,infinity)};
}
inline Interval ppm(Interval value) {
    const auto infinity=std::numeric_limits<double>::infinity();
    return {value.lower==0?0:std::nextafter(value.lower/1e6,-infinity),
            value.upper==0?0:std::nextafter(value.upper/1e6,infinity)};
}
inline Interval strip(double offset,double uncertainty,double delta,double lower,double upper) {
    const auto at=[&](double clock) {
        const auto scaled=ppm(product(delta,clock));
        return Interval{sum(offset,scaled.lower).lower,sum(offset,scaled.upper).upper};
    };
    const auto a=at(lower),b=at(upper);
    return {sum(std::min(a.lower,b.lower),-uncertainty).lower,
            sum(std::max(a.upper,b.upper),uncertainty).upper};
}
inline Interval inner_cell(double center,double origin,double radius) {
    const auto translated=sum(center,origin);
    return {sum(translated.upper,-radius).upper,sum(translated.lower,radius).lower};
}
inline Interval inner_clock(double center,double radius,double physical) {
    return {std::max(-physical,sum(center,-radius).upper),
            std::min(physical,sum(center,radius).lower)};
}
} // namespace helper_prior_detail

// A and beta are independently authoritative: A is the SAME original
// builder's requested/admitted physical LF halfwidth (including UTC steering),
// beta is its represented timing-cell halfwidth. Missing provenance must be
// declined by the caller; zero is a known point-domain, not missing metadata.
inline HelperPriorMask helper_prior_mask(std::span<const modem::PatternFrequencyRateHypothesis> pairs,
        double offset_hz,double uncertainty_hz,double data_minus_helper_hz,
        double frequency_cell_radius_hz,double clock_cell_half_width_ppm,
        double physical_clock_half_width_ppm) {
    using namespace helper_prior_detail;
    HelperPriorMask result;
    result.clock_cell_half_width_ppm=clock_cell_half_width_ppm;
    if(pairs.empty() || pairs.size()>result.cells.size() || !std::isfinite(offset_hz) ||
       !std::isfinite(uncertainty_hz) || uncertainty_hz<0 || !std::isfinite(data_minus_helper_hz) ||
       !std::isfinite(frequency_cell_radius_hz) || frequency_cell_radius_hz<0 ||
       !std::isfinite(clock_cell_half_width_ppm) || clock_cell_half_width_ppm<0 ||
       !std::isfinite(physical_clock_half_width_ppm) || physical_clock_half_width_ppm<0)return result;
    if(!environment()) {
        result.reason="helper interval arithmetic is unsupported; broad acquisition retained";
        return result;
    }
    // All storage is temporary setup scratch, not the retained 7 KiB helper
    // cohort/observer allowance. No persistent field or heap owner is added.
    static_assert(modem::maximum_pattern_frequency_rate_hypotheses<=
        std::numeric_limits<std::uint16_t>::max());
    std::array<std::uint16_t,modem::maximum_pattern_frequency_rate_hypotheses> order{};
    std::array<double,modem::maximum_pattern_rate_hypotheses> rates{},nearest{};
    std::array<Interval,modem::maximum_pattern_rate_hypotheses> clocks{};
    std::array<double,2*modem::maximum_pattern_rate_hypotheses+2> endpoints{};
    for(const auto& pair:pairs) {
        if(!std::isfinite(pair.frequency_offset_hz) || !std::isfinite(pair.clock_error_ppm))return result;
        const auto end=rates.begin()+result.rates;
        if(std::find(rates.begin(),end,pair.clock_error_ppm)==end) {
            if(result.rates==rates.size())return result;
            rates[result.rates]=pair.clock_error_ppm;
            clocks[result.rates]=inner_clock(pair.clock_error_ppm,clock_cell_half_width_ppm,
                physical_clock_half_width_ppm);
            if(!finite(clocks[result.rates]))return result;
            ++result.rates;
        }
    }
    // Diagnostic radius only. The actual certificate below uses the complete
    // 2D strip, not a nearest-lane or scalar-extrema approximation.
    result.radius_hz=uncertainty_hz+std::abs(data_minus_helper_hz)*clock_cell_half_width_ppm*1e-6+
        frequency_cell_radius_hz;
    if(!std::isfinite(result.radius_hz))return result;
    std::size_t endpoint_count=0;
    endpoints[endpoint_count++]=-physical_clock_half_width_ppm;
    endpoints[endpoint_count++]=physical_clock_half_width_ppm;
    for(std::size_t r=0;r<result.rates;++r)if(clocks[r].lower<=clocks[r].upper) {
        if(clocks[r].lower>-physical_clock_half_width_ppm && clocks[r].lower<physical_clock_half_width_ppm)
            endpoints[endpoint_count++]=clocks[r].lower;
        if(clocks[r].upper>-physical_clock_half_width_ppm && clocks[r].upper<physical_clock_half_width_ppm)
            endpoints[endpoint_count++]=clocks[r].upper;
    }
    std::sort(endpoints.begin(),endpoints.begin()+endpoint_count);
    endpoint_count=static_cast<std::size_t>(std::unique(endpoints.begin(),endpoints.begin()+endpoint_count)-endpoints.begin());

    const auto certifies=[&](double origin) {
        result.cells.fill(0);result.selected=0;result.measured_selected=0;result.represented_rates=0;
        result.origin_hz=origin;
        std::fill(nearest.begin(),nearest.begin()+result.rates,std::numeric_limits<double>::infinity());
        // Select original cells intersecting the mapped strip, then preserve
        // nearest ties for every original LF value. Only the union proof below
        // can authorize assistance; nearest extras alone prove no coverage.
        for(std::size_t i=0;i<pairs.size();++i) {
            const auto& pair=pairs[i];
            const auto r=static_cast<std::size_t>(std::find(rates.begin(),rates.begin()+result.rates,pair.clock_error_ppm)-rates.begin());
            const auto cell=inner_cell(pair.frequency_offset_hz,origin,frequency_cell_radius_hz);
            if(!finite(cell))return false;
            const auto predicted=static_cast<long double>(offset_hz)+
                static_cast<long double>(pair.clock_error_ppm)*data_minus_helper_hz/1e6L;
            const auto distance=std::abs(static_cast<long double>(pair.frequency_offset_hz)+origin-predicted);
            nearest[r]=std::min(nearest[r],static_cast<double>(distance));
            if(clocks[r].lower>clocks[r].upper || cell.lower>cell.upper)continue;
            const auto required=strip(offset_hz,uncertainty_hz,data_minus_helper_hz,clocks[r].lower,clocks[r].upper);
            if(!finite(required))return false;
            if(cell.lower<=required.upper && cell.upper>=required.lower) {
                result.cells[i]=1;++result.selected;
            }
        }
        result.measured_selected=result.selected;
        for(std::size_t i=0;i<pairs.size();++i) {
            const auto& pair=pairs[i];
            const auto r=static_cast<std::size_t>(std::find(rates.begin(),rates.begin()+result.rates,pair.clock_error_ppm)-rates.begin());
            const auto predicted=static_cast<long double>(offset_hz)+
                static_cast<long double>(pair.clock_error_ppm)*data_minus_helper_hz/1e6L;
            const auto distance=std::abs(static_cast<long double>(pair.frequency_offset_hz)+origin-predicted);
            // This tolerance adds optional work only; it is NEVER used to
            // bridge a clock/RF gap in the coverage certificate.
            const auto tolerance=64*std::numeric_limits<double>::epsilon()*
                std::max({1.L,std::abs(static_cast<long double>(origin)),std::abs(predicted),distance});
            if(distance<=nearest[r]+tolerance && !result.cells[i]) {result.cells[i]=1;++result.selected;}
        }
        for(std::size_t r=0;r<result.rates;++r)
            for(std::size_t i=0;i<pairs.size();++i)if(result.cells[i] && pairs[i].clock_error_ppm==rates[r]) {
                ++result.represented_rates;break;
            }
        std::size_t count=0;
        for(std::size_t i=0;i<pairs.size();++i)if(result.cells[i])order[count++]=static_cast<std::uint16_t>(i);
        std::sort(order.begin(),order.begin()+count,[&](auto a,auto b) {
            return pairs[a].frequency_offset_hz<pairs[b].frequency_offset_hz;
        });
        const auto slab_covered=[&](double lower,double upper) {
            const auto required=strip(offset_hz,uncertainty_hz,data_minus_helper_hz,lower,upper);
            if(!finite(required))return false;
            auto covered=required.lower;bool reached=false;
            for(std::size_t at=0;at<count;++at) {
                const auto& pair=pairs[order[at]];
                const auto clock=inner_clock(pair.clock_error_ppm,clock_cell_half_width_ppm,physical_clock_half_width_ppm);
                if(clock.lower>lower || clock.upper<upper)continue;
                const auto cell=inner_cell(pair.frequency_offset_hz,origin,frequency_cell_radius_hz);
                if(!finite(cell) || cell.lower>cell.upper)return false;
                if(cell.upper<required.lower)continue;
                if(cell.lower>covered)return false; // real or unproved gap
                covered=std::max(covered,cell.upper);reached=true;
                if(covered>=required.upper)return true;
            }
            return reached && covered>=required.upper;
        };
        // A known zero physical LF domain is a point, never vacuous success.
        if(endpoint_count==1)return slab_covered(endpoints[0],endpoints[0]);
        for(std::size_t at=1;at<endpoint_count;++at)
            if(!slab_covered(endpoints[at-1],endpoints[at]))return false;
        return true;
    };
    // A center inside the old RF extrema is insufficient: the entire CI can
    // cross an edge. Recenter the SAME catalogue only after the full strip
    // fails at zero. The phase caller continues to supply its frozen baseline.
    if(!certifies(0) && (offset_hz==0 || !certifies(offset_hz))) {
        result.reason="helper full uncertainty strip is not covered by the original frequency/clock cells; broad acquisition retained";
        return result;
    }
    if(!result.selected || result.represented_rates!=result.rates ||
       (result.selected==pairs.size() && result.origin_hz==0)) {
        result.reason=result.selected==pairs.size()?"helper selects the complete unshifted catalogue; no acquisition-work reduction":
            "helper cannot retain every original clock lane; broad acquisition retained";
        return result;
    }
    result.admitted=true;
    result.reason=result.selected<pairs.size()?"measured helper selects certified original acquisition cells":
        "measured helper aligns the certified original acquisition catalogue without reducing its work";
    return result;
}

} // namespace datapump::live::detail
