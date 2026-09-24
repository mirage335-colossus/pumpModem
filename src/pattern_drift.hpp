#pragma once

#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace datapump::modem::detail {

// Partition nominal symbol coordinates before applying a clock hypothesis.
// Quotient/remainder arithmetic avoids overflowing index * total.
inline std::uint64_t drift_boundary(unsigned index,std::uint64_t total,unsigned sections) {
    if(!sections)return 0;
    index=std::min(index,sections);
    return total/sections*index+(total%sections)*index/sections;
}

inline unsigned drift_section_count(const Config& config,bool enabled=true) {
    if(!enabled || !config.pattern_symbols || config.spreading_mode==SpreadingMode::tone)return 1;
    const auto total=symbol_sample_count(config),chip=pattern_chip_samples(config);
    if(static_cast<long double>(total)<16.L*config.sample_rate || total/chip<64)return 1;
    for(unsigned section=0;section<4;++section) {
        const auto begin=drift_boundary(section,total,4),end=drift_boundary(section+1,total,4);
        const auto first=begin/chip+(begin%chip!=0);
        if(end/chip<first || end/chip-first<16)return 1;
    }
    return 4;
}

// A K-section fit has K complex (2K real) free coefficients. Under the same
// white-noise model as the coherent scorer, explained / total energy has a
// Beta(K, N-K) complex or Beta(K, N/2-K) real null distribution. Conditioning
// corrections belong to the caller's fitted energy, before this calculation.
inline double drift_evidence(double explained,double energy,double effective_count,
                             unsigned sections,bool real_rank) {
    if(!sections || sections>4 || !(energy>1e-30) || !(explained>0) ||
            !std::isfinite(energy) || !std::isfinite(explained) || !std::isfinite(effective_count))return 0;
    const auto residual=(real_rank?.5:1.)*effective_count-sections;
    if(!(residual>0))return 0;
    const auto fraction=std::clamp(explained/energy,0.,1.-1e-15);
    if(!(fraction>0))return 0;
    // For integer K the survival function is (1-q)^b times a K-term
    // polynomial. Log-sum-exp keeps the calculation bounded at large N.
    std::array<double,4> terms{};
    double largest=0;
    for(unsigned j=1;j<sections;++j) {
        terms[j]=terms[j-1]+std::log(residual+j-1)+std::log(fraction)-std::log(static_cast<double>(j));
        largest=std::max(largest,terms[j]);
    }
    double sum=0;
    for(unsigned j=0;j<sections;++j)sum+=std::exp(terms[j]-largest);
    return std::max(0.,-residual*std::log1p(-fraction)-largest-std::log(sum));
}

inline double combine_drift_evidence(double coherent,double drift,unsigned sections) {
    // A union bound accounts for trying both predetermined detectors. The
    // ineligible path returns its historical score without any rounding change.
    return sections<=1?coherent:std::max(0.,std::max(coherent,drift)-std::numbers::ln2);
}

} // namespace datapump::modem::detail
