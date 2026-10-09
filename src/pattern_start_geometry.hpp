#pragma once

#include "datapump/pattern_receiver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace datapump::modem::detail {

struct PatternStartPhases {
    std::uint64_t lower=0,upper=0;
    std::size_t count=0;
};

// The compact receiver's existing timing lattice, including its clipped final
// origin. A UTC prior only intersects original coverage cells and phase values;
// it never translates an origin or changes its original index/parity.
class PatternStartLattice {
public:
    PatternStartLattice(long double lower,long double upper,long double step,
                        std::uint64_t phase_lower,std::uint64_t phase_upper,
                        std::uint64_t phase_step,
                        std::optional<PatternStartWindow> window=std::nullopt)
        :lower_(lower),upper_(upper),step_(step),phase_lower_(phase_lower),
         phase_upper_(phase_upper),phase_step_(phase_step),window_(window) {
        if(!std::isfinite(lower) || !std::isfinite(upper) || lower>upper ||
           std::abs(lower)>=1e15L || std::abs(upper)>=1e15L ||
           !std::isfinite(step) || step<=0 || phase_lower>phase_upper || !phase_step)
            throw std::invalid_argument("invalid pattern start lattice");
        const auto count=std::ceil((upper-lower)/step)+1;
        if(!std::isfinite(count) || count<1 ||
           count>=static_cast<long double>(std::numeric_limits<std::size_t>::max()))
            throw std::length_error("pattern start lattice exceeds address space");
        origins_=static_cast<std::size_t>(count);
        const auto phase_intervals=(phase_upper-phase_lower)/phase_step;
        if(phase_intervals>=std::numeric_limits<std::size_t>::max())
            throw std::length_error("pattern phase lattice exceeds address space");
        phases_=static_cast<std::size_t>(phase_intervals)+1;
        retained_={0,origins_};
        if(!window_)return;
        if(!std::isfinite(window_->epoch_origin_samples) ||
           !std::isfinite(window_->phase_scale) || window_->phase_scale<=0 ||
           !std::isfinite(window_->half_width_samples) || window_->half_width_samples<0)
            throw std::invalid_argument("invalid qualified UTC start map");
        // This padding is an upper bound on every cell's original local pad.
        // Its constant value makes the broad envelope monotonic, so a tight
        // prior costs O(log(full origins)) before exact local intersections.
        const auto pad=64*std::numeric_limits<long double>::epsilon()*
            std::max({1.L,std::abs(lower_),std::abs(upper_),
                      std::abs(window_->epoch_origin_samples),window_->half_width_samples});
        retained_.first=first_true([&](std::size_t i) {
            const auto cell=coverage_cell(i);
            return (cell.second-window_->epoch_origin_samples+
                    window_->half_width_samples+pad)/window_->phase_scale>=phase_lower_;
        });
        retained_.second=first_true([&](std::size_t i) {
            const auto cell=coverage_cell(i);
            return (cell.first-window_->epoch_origin_samples-
                    window_->half_width_samples-pad)/window_->phase_scale>phase_upper_;
        });
        if(retained_.second<retained_.first)retained_.second=retained_.first;
    }

    std::size_t full_origin_count() const noexcept {return origins_;}
    std::size_t phase_count() const noexcept {return phases_;}
    std::size_t full_phase_count() const {return checked_product(origins_,phases_);}
    // Half-open range of original indices. Some entries can still have no
    // retained phase when the phase lattice has gaps; call retained_phases().
    std::pair<std::size_t,std::size_t> retained_index_range() const noexcept {return retained_;}
    long double origin(std::size_t i) const {
        if(i>=origins_)throw std::out_of_range("pattern start origin index");
        return std::min(upper_,lower_+static_cast<long double>(i)*step_);
    }
    std::pair<long double,long double> coverage_cell(std::size_t i) const {
        const auto current=origin(i);
        const auto previous=i?origin(i-1):lower_;
        const auto next=i+1<origins_?origin(i+1):upper_;
        return {i?(previous+current)/2:lower_,i+1<origins_?(current+next)/2:upper_};
    }
    std::optional<PatternStartPhases> retained_phases(std::size_t i) const {
        if(i>=origins_)throw std::out_of_range("pattern start origin index");
        if(i<retained_.first || i>=retained_.second)return std::nullopt;
        if(!window_)return PatternStartPhases{phase_lower_,phase_upper_,phases_};
        const auto cell=coverage_cell(i);
        const auto pad=64*std::numeric_limits<long double>::epsilon()*
            std::max({1.L,std::abs(cell.first),std::abs(cell.second),
                      std::abs(window_->epoch_origin_samples),window_->half_width_samples});
        auto lo=(cell.first-window_->epoch_origin_samples-window_->half_width_samples-pad)/window_->phase_scale;
        auto hi=(cell.second-window_->epoch_origin_samples+window_->half_width_samples+pad)/window_->phase_scale;
        if(lo>phase_upper_ || hi<phase_lower_)return std::nullopt;
        lo=std::clamp(lo,static_cast<long double>(phase_lower_),static_cast<long double>(phase_upper_));
        hi=std::clamp(hi,static_cast<long double>(phase_lower_),static_cast<long double>(phase_upper_));
        const auto first=std::ceil((lo-phase_lower_)/phase_step_);
        const auto last=std::floor((hi-phase_lower_)/phase_step_);
        if(first>last)return std::nullopt;
        const auto first_index=static_cast<std::uint64_t>(first);
        const auto last_index=static_cast<std::uint64_t>(last);
        return PatternStartPhases{phase_lower_+first_index*phase_step_,
            phase_lower_+last_index*phase_step_,static_cast<std::size_t>(last_index-first_index)+1};
    }
    std::size_t retained_origin_count() const {
        if(!window_)return origins_;
        std::size_t result=0;
        for(auto i=retained_.first;i<retained_.second;++i)if(retained_phases(i))++result;
        return result;
    }
    std::size_t retained_phase_count() const {
        if(!window_)return full_phase_count();
        std::size_t result=0;
        for(auto i=retained_.first;i<retained_.second;++i)if(const auto phases=retained_phases(i)) {
            if(phases->count>std::numeric_limits<std::size_t>::max()-result)
                throw std::length_error("retained pattern phases exceed address space");
            result+=phases->count;
        }
        return result;
    }
private:
    template<class Predicate> std::size_t first_true(Predicate predicate) const {
        std::size_t low=0,high=origins_;
        while(low<high) {
            const auto middle=low+(high-low)/2;
            if(predicate(middle))high=middle;else low=middle+1;
        }
        return low;
    }
    static std::size_t checked_product(std::size_t a,std::size_t b) {
        if(a && b>std::numeric_limits<std::size_t>::max()/a)
            throw std::length_error("pattern start phase bank exceeds address space");
        return a*b;
    }
    long double lower_=0,upper_=0,step_=1;
    std::uint64_t phase_lower_=0,phase_upper_=0,phase_step_=1;
    std::optional<PatternStartWindow> window_;
    std::size_t origins_=0,phases_=0;
    std::pair<std::size_t,std::size_t> retained_;
};

} // namespace datapump::modem::detail
