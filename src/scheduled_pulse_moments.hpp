#pragma once

#include "pattern_correlator_batch.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace datapump::modem::detail {

// Powers use n relative to the actual physical span's first sample. Oscillator
// reset/origin and real-PCM energy stay unchanged; nulls are never concatenated.
struct ScheduledSpanMoments {
    std::array<std::complex<double>,2> signal{};
    std::array<std::complex<double>,3> square{};
    std::array<double,3> norm{};
    double energy=0;
    std::uint64_t count=0;
};

template<class PrefixMoment>
bool scheduled_span_moments(std::span<const CorrelationProjection> physical,
        std::span<const PrefixMoment> powers,std::size_t left,std::size_t count,
        ScheduledSpanMoments& result) {
    // A shared physical prefix may expose only the current partial block;
    // the owned power buffer can retain the full allocated block capacity.
    if(!count || left>=physical.size() || count>=physical.size()-left ||
            left>=powers.size() || count>=powers.size()-left)return false;
    const auto p=physical[left+count]-physical[left];
    const auto& a=powers[left];const auto& b=powers[left+count];
    const double l=static_cast<double>(left);
    ScheduledSpanMoments out;
    out.signal[0]={p.xc,p.xs};
    out.signal[1]=b.signal1-a.signal1-l*out.signal[0];
    out.square[0]={p.cc-p.ss,2*p.cs};
    out.square[1]=b.square1-a.square1-l*out.square[0];
    out.square[2]=b.square2-a.square2-2*l*(b.square1-a.square1)+l*l*out.square[0];
    out.norm[0]=p.cc+p.ss;
    out.norm[1]=b.norm1-a.norm1-l*out.norm[0];
    out.norm[2]=b.norm2-a.norm2-2*l*(b.norm1-a.norm1)+l*l*out.norm[0];
    out.energy=p.energy;out.count=count;
    result=out;return true;
}

// Atomic two-bit output: a rejected Gram cannot leave a sibling or detector
// partly updated. The original sampled contraction owns the entire fallback.
inline bool scheduled_affine_pair(const ScheduledSpanMoments& m,
        const std::array<std::complex<double>,2>& value,
        const std::array<std::complex<double>,2>& slope,
        std::array<CorrelationFit,2>& result,
        const std::array<std::complex<double>,2>* signal_override=nullptr,
        const std::array<CorrelationFit,2>* gram_override=nullptr) {
    if(!m.count)return false;
    std::array<CorrelationFit,2> out;
    for(unsigned bit=0;bit<2;++bit) {
        const auto v=value[bit],s=slope[bit];
        const auto dot=signal_override?(*signal_override)[bit]:v*m.signal[0]+s*m.signal[1];
        auto& f=out[bit];double trace;
        if(gram_override) {
            f=(*gram_override)[bit];f.xc=dot.real();f.xs=dot.imag();
            f.energy=m.energy;f.count=m.count;trace=f.cc+f.ss;
        } else {
            trace=std::norm(v)*m.norm[0]+
                2*(v*std::conj(s)).real()*m.norm[1]+std::norm(s)*m.norm[2];
            const auto square=v*v*m.square[0]+2.*v*s*m.square[1]+s*s*m.square[2];
            f={dot.real(),dot.imag(),(trace+square.real())/2,
                (trace-square.real())/2,square.imag()/2,m.energy,m.count};
        }
        const auto determinant=f.cc*f.ss-f.cs*f.cs,scale=trace*trace;
        if(!std::isfinite(trace) || !std::isfinite(determinant) ||
           !std::isfinite(scale) || !std::isfinite(trace+f.xc+f.xs+f.cs) ||
           !std::isfinite(f.xc) || !std::isfinite(f.xs) ||
           !std::isfinite(f.cc) || !std::isfinite(f.ss) ||
           !std::isfinite(f.cs) || !std::isfinite(f.energy) ||
           f.cc<=0 || f.ss<=0 || determinant<=1e-10*scale)return false;
    }
    result=out;return true;
}

// Source pulse geometry is independent of frequency and private coefficients.
// Valid positions are ascending; the clipped partial final chip keeps its own
// center, amplitude and table knots. Closed support endpoints are singletons.
struct ScheduledPulseSpan {
    std::array<double,correlation_pulse_atoms> value{},slope{};
    std::array<std::uint64_t,correlation_pulse_atoms> position{};
    std::uint32_t valid=0;
    std::uint64_t count=0;
    bool endpoint=false;
};

// Conservative scratch reservation per executing worker, including both wide
// pulse pieces, caller/return values, moments and atomic fit candidates.
inline constexpr std::size_t scheduled_outer_pulse_scratch_bytes=4096;
static_assert(2*sizeof(CorrelationPulseSegment)+sizeof(ScheduledPulseSpan)+
    sizeof(ScheduledSpanMoments)+2*sizeof(CorrelationFit)+512<=
    scheduled_outer_pulse_scratch_bytes);

inline ScheduledPulseSpan scheduled_pulse_span(long double within,
        long double rate,std::uint64_t chip,std::uint64_t symbol,
        std::uint64_t maximum) {
    if(!chip || !symbol || !maximum || !std::isfinite(within) ||
        within<0 || within>=symbol || !std::isfinite(rate) || rate<=0)
        throw Error("invalid scheduled pulse span geometry");
    const auto local=static_cast<std::uint64_t>(within/chip);
    const auto chips=symbol/chip+(symbol%chip!=0);
    const auto duration=static_cast<long double>(chip)/rate;
    const auto offset=(within-static_cast<long double>(local)*chip)/rate;
    auto piece=correlation_pulse_segment(offset,maximum,duration);
    const auto tail=symbol%chip,final_chip=chips-1;
    const bool shifted=tail && final_chip>=local && final_chip-local<=8;
    CorrelationPulseSegment final_piece;
    if(shifted) {
        final_piece=correlation_pulse_segment(offset+
            static_cast<long double>(chip-tail)/(2*rate),maximum,duration);
        piece.count=std::min(piece.count,final_piece.count);
    }
    ScheduledPulseSpan out;out.count=piece.count;
    out.endpoint=piece.endpoint || (shifted && final_piece.endpoint);
    for(std::size_t j=0;j<correlation_pulse_atoms;++j) {
        if(j<8 && local<8-j)continue;
        if(j>8 && local>std::numeric_limits<std::uint64_t>::max()-(j-8))continue;
        const auto position=j<8?local-(8-j):local+(j-8);
        if(position>=chips)continue;
        auto a=piece.value[j],b=piece.slope[j];
        if(shifted && position==final_chip) {
            const auto scale=std::sqrt(static_cast<double>(tail)/static_cast<double>(chip));
            a=final_piece.value[j]*scale;b=final_piece.slope[j]*scale;
        }
        if(a==0 && b==0)continue;
        out.valid|=std::uint32_t{1}<<j;out.position[j]=position;
        out.value[j]=static_cast<double>(a);out.slope[j]=static_cast<double>(b);
    }
    return out;
}

// Apply only after both main fits passed. Every fragment contributes to the
// SAME existing complete-pulse ring before any variance/score is formed.
// coefficients(position) uses the current/carry symbol's original absolute
// address and retains independent private candidates. Exact gain is flat one.
template<class Coefficients>
void scheduled_outer_piece(const ScheduledSpanMoments& m,
        const ScheduledPulseSpan& pulse,CorrelationOuterEvidence& outer,
        Coefficients coefficients) {
    if(m.count!=pulse.count)throw Error("scheduled outer pulse span count mismatch");
    for(std::size_t j=0;j<correlation_pulse_atoms;++j)
        if(pulse.valid&(std::uint32_t{1}<<j))
            outer.add(pulse.position[j],pulse.value[j]*m.signal[0]+
                pulse.slope[j]*m.signal[1],coefficients);
}

// Keep the newly reserved outer-only frame OUT of the ordinary sampled
// accumulator, including when workspace eviction disables this backend.
// Supported native compilers honor this outline even with optimization/LTO.
#if defined(_MSC_VER)
#define DATAPUMP_SCHEDULED_OUTLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DATAPUMP_SCHEDULED_OUTLINE __attribute__((noinline))
#else
#error "scheduled outer pulse scratch needs a qualified noinline annotation"
#endif
template<class Apply>
DATAPUMP_SCHEDULED_OUTLINE std::uint64_t scheduled_with_outer_span(
        long double within,long double rate,std::uint64_t chip,
        std::uint64_t symbol,std::uint64_t maximum,Apply apply) {
    const auto pulse=scheduled_pulse_span(within,rate,chip,symbol,maximum);
    if(pulse.count<=1)return 0;
    ScheduledSpanMoments accepted;
    return apply(pulse,accepted);
}
#undef DATAPUMP_SCHEDULED_OUTLINE

// Optional exact-span tile, not enabled by the integration patch. Charge its
// entire size before use. One worker owns a tile and consumes each returned
// value before another get. Reset before EVERY distinct physical capture block;
// matching PCM bytes or a recycled pointer do not establish capture identity.
template<std::size_t Slots=32> class ScheduledMomentTile {
    static_assert(Slots && (Slots&(Slots-1))==0);
    struct Entry {
        std::uint64_t frequency_bits=0;
        std::size_t left=0,count=0;
        ScheduledSpanMoments value;
        bool valid=false;
    };
    std::array<Entry,Slots> rows_{};
public:
    std::uint64_t hits=0,misses=0,replacements=0;
    void reset(){for(auto& row:rows_)row.valid=false;}
    template<class Builder>
    const ScheduledSpanMoments* get(std::uint64_t frequency_bits,
            std::size_t left,std::size_t count,Builder build) {
        const auto mixed=frequency_bits^(frequency_bits>>32)^left^
            (count*std::size_t{0x9e3779b9});
        auto& row=rows_[mixed&(Slots-1)];
        if(row.valid && row.frequency_bits==frequency_bits &&
                row.left==left && row.count==count){++hits;return &row.value;}
        ++misses;ScheduledSpanMoments next;
        if(!build(next))return nullptr;
        if(row.valid)++replacements;
        row.frequency_bits=frequency_bits;row.left=left;row.count=count;
        row.value=next;row.valid=true;return &row.value;
    }
};

} // namespace datapump::modem::detail
