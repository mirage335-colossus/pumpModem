#pragma once
// Internal allocation-free source row used by coherent continuation only.
// The caller owns one exact source phase/index and prepares that symbol first.
// RF is deliberately applied later, in the original per-carrier accumulation.
#include "datapump/pattern_code.hpp"
#include <array>
#include <complex>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>

namespace datapump::modem::detail {
inline std::size_t pattern_continuation_source_row(PatternCode& code,std::uint64_t index,
        std::size_t bin_samples,std::size_t length,double ratio,bool variable_clock,bool shaped,
        std::span<const std::array<std::complex<double>,2>> nominal,
        std::span<std::complex<double>> product,std::stop_token stop={}) {
    if(!bin_samples || product.size()/2<length || (!nominal.empty() && nominal.size()<length))
        throw Error("pattern continuation source scratch is incomplete");
    std::size_t valid=length;
    for(std::size_t i=0;i<length;++i) {
        if((i&4095U)==0 && stop.stop_requested())throw Error("pattern search cancelled");
        const auto sample_position=static_cast<long double>(i)*bin_samples+static_cast<long double>(bin_samples-1)/2;
        const auto source_position=sample_position*ratio;
        if(variable_clock && source_position>=code.symbol_samples()){valid=i;break;}
        const auto chip_position=source_position/code.chip_samples();
        const auto local=static_cast<std::uint64_t>(chip_position);
        if(index>(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol())
            throw Error("pattern stream coordinate overflow");
        const auto chip=index*code.chips_per_symbol()+local;
        const auto fraction=static_cast<double>(chip_position-local);
        const bool cached=ratio==1 && !nominal.empty();
        std::array<std::complex<double>,2> values;
        if(cached)values={nominal[i][0],nominal[i][1]};
        else if(shaped)values=code.shaped_values(index*code.chips_per_symbol(),static_cast<double>(source_position));
        else values=code.values(chip,fraction);
        product[2*i]=values[0];product[2*i+1]=values[1];
    }
    return valid;
}
}
