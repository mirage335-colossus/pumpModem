#pragma once

#include "datapump/pattern_pulse.hpp"
#include <array>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <complex>
#include <limits>

namespace datapump::modem::detail {

// Public geometry from one executed Original pulse traversal. PatternCode owns
// this scratch (and charges it in working_bytes); it contains no private chips.
// A borrowed row is valid only until the next shaping operation on that code.
struct OriginalPulseRow {
    struct Atom { std::uint64_t local=0; double weight=0; };
    static constexpr std::size_t capacity=2*pattern_pulse_half_span+2;
    std::array<Atom,capacity> atoms{};
    std::uint64_t argument_bits=0,symbol=0,chip=0;
    std::size_t count=0;
    bool complete=false,overflow=false;

    void begin(double argument,std::uint64_t samples,std::uint64_t chip_samples) noexcept {
        argument_bits=std::bit_cast<std::uint64_t>(argument);
        symbol=samples;chip=chip_samples;count=0;complete=false;overflow=false;
    }
    void append(std::uint64_t local,double weight) noexcept {
        if(count<atoms.size())atoms[count++]={local,weight};
        else overflow=true;
    }
    void finish() noexcept { complete=!overflow; }
    template<class Evidence,class Coefficients>
    bool accumulate_outer(double argument,std::uint64_t samples,std::uint64_t chip_samples,
            std::uint64_t first_chip,Evidence& target,double gain,std::complex<double> projection,
            Coefficients coefficients) const {
        // Validate the WHOLE row before mutating a live ring. Sparse rows may
        // fit; a row spanning more than one ring window must use scalar add().
        if(!complete || overflow || count>atoms.size() || symbol!=samples || chip!=chip_samples ||
           argument_bits!=std::bit_cast<std::uint64_t>(argument) || !samples || !chip_samples ||
           target.pulse.size()!=17)return false;
        if(!count)return true;
        const auto chips=samples/chip_samples+(samples%chip_samples!=0);
        if(first_chip>std::numeric_limits<std::uint64_t>::max()-(chips-1))return false;
        for(std::size_t i=0;i<count;++i) {
            if(atoms[i].local>=chips || !std::isfinite(atoms[i].weight) || atoms[i].weight==0 ||
               (i && atoms[i].local<=atoms[i-1].local))return false;
        }
        const auto minimum=atoms[0].local,maximum=atoms[count-1].local;
        if(maximum-minimum>=target.pulse.size() || (target.initialized && minimum<target.first))return false;
        if(!target.initialized) {
            target.first=minimum>16?minimum-16:0;target.last=minimum;target.initialized=true;
        }
        // Every flushed index is <= maximum-17 < minimum. This row cannot
        // contribute to it. Moving these SAME ascending flushes before the row
        // preserves their pulse values and all long-double guard arithmetic.
        while(maximum-target.first>=target.pulse.size())target.flush(coefficients);
        auto cursor=minimum%target.pulse.size();auto previous=minimum;
        for(std::size_t i=0;i<count;++i) {
            cursor+=atoms[i].local-previous;
            if(cursor>=target.pulse.size())cursor-=target.pulse.size();
            target.pulse[cursor]+=(gain*atoms[i].weight)*projection;
            previous=atoms[i].local;
        }
        target.last=std::max(target.last,maximum);return true;
    }
    template<class Consume>
    bool consume(double argument,std::uint64_t samples,std::uint64_t chip_samples,
                 Consume&& consume) const {
        // Check the entire capability before changing a live outer ring.
        if(!complete || overflow || count>atoms.size() || symbol!=samples || chip!=chip_samples ||
           argument_bits!=std::bit_cast<std::uint64_t>(argument) || !samples || !chip_samples)return false;
        const auto chips=samples/chip_samples+(samples%chip_samples!=0);
        for(std::size_t i=0;i<count;++i)if(atoms[i].local>=chips || !std::isfinite(atoms[i].weight) ||
            atoms[i].weight==0 || (i && atoms[i].local<=atoms[i-1].local))return false;
        for(std::size_t i=0;i<count;++i)consume(atoms[i].local,atoms[i].weight);
        return true;
    }
};

} // namespace datapump::modem::detail
