#pragma once

// Rejection certificates for the EXISTING CorrelationChipEvidence detector.
// No proxy state may be installed into a receiver or reported as absence.
// Unknown rank, ownership, field errors or libm contracts return infinity.
#include "scheduled_acquisition_certificate.hpp"
#include "scheduled_acquisition_tile.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem::detail::scheduled_cold_colored {
namespace cert=scheduled_acquisition_certificate;
using cert::Interval;
inline constexpr auto no_owner=std::numeric_limits<std::uint64_t>::max();

struct FitIntervals {
    Interval xc,xs,cc,ss,cs,energy;
    std::uint64_t count=0;
    std::uint64_t count_lower=0,count_upper=0;
    bool valid=false,zero_basis=false;
};
inline bool finite(const FitIntervals& f) {
    return f.valid && f.xc.bounded() && f.xs.bounded() && f.cc.bounded() &&
        f.ss.bounded() && f.cs.bounded() && f.energy.bounded() && f.energy.lo>=0 &&
        f.count_lower<=f.count && f.count<=f.count_upper && f.count_upper<=0x1fffffffffffffULL;
}
inline FitIntervals enclose(const CorrelationFit& f,double dot,double gram,double energy,
                            bool exact_zero=false,std::uint64_t count_radius=0) {
    if(f.count>0x1fffffffffffffULL || !std::isfinite(dot) || dot<0 || !std::isfinite(gram) || gram<0 ||
       !std::isfinite(energy) || energy<0)return {};
    FitIntervals result{Interval::disk(f.xc,dot),Interval::disk(f.xs,dot),
        Interval::disk(f.cc,gram),Interval::disk(f.ss,gram),Interval::disk(f.cs,gram),
        Interval::disk(f.energy,energy),f.count,
        f.count>count_radius?f.count-count_radius:0,
        count_radius<=0x1fffffffffffffULL-f.count?f.count+count_radius:std::numeric_limits<std::uint64_t>::max(),
        true,exact_zero};
    result.energy.lo=std::max(0.,result.energy.lo);
    if(exact_zero) {
        // Caller proves the ORIGINAL coefficient is exactly zero on every
        // observation; a zero proxy value alone does not establish this.
        if(count_radius || !CorrelationChipEvidence::zero_basis(f))return {};
        result.xc=result.xs=result.cc=result.ss=result.cs=Interval{};
    }
    return finite(result)?result:FitIntervals{};
}
inline Interval determinant(const FitIntervals& f) {
    return cert::minus(cert::times(f.cc,f.ss),cert::times(f.cs,f.cs));
}
inline Interval numerator(const FitIntervals& f) {
    // Preserve the executed original source association, including rounded
    // x*x and y*y products (not an algebraically equivalent quadratic form).
    return cert::minus(cert::plus(cert::times(cert::times(f.ss,f.xc),f.xc),
        cert::times(cert::times(f.cc,f.xs),f.xs)),
        cert::times(cert::times(cert::times(Interval::point(2),f.cs),f.xc),f.xs));
}
inline Interval clamp_energy(Interval value,Interval energy) {
    return {std::max(0.,std::min(value.lo,energy.lo)),
            std::max(0.,std::min(value.hi,energy.hi))};
}
struct Projection {Interval energy;unsigned rank=0;bool valid=false,nested=false;};
inline Projection projection(const FitIntervals& f) {
    if(!finite(f))return {};
    const auto trace=cert::plus(f.cc,f.ss);
    if(!trace.bounded())return {};
    if(!f.count_upper || trace.hi<=1e-30)return {{},0,true,f.zero_basis || !f.count_upper};
    if(f.zero_basis)return {{},0,true,true};
    if(trace.lo<=1e-30)return {};
    const auto det=determinant(f);
    // trace*trace follows the ORIGINAL branch, not the looser coherent cutoff.
    const auto cutoff=cert::times(cert::times(Interval::point(1e-12),trace),trace);
    if(f.count_lower<=1 || !det.bounded() || !cutoff.bounded() || det.lo<=cutoff.hi)return {};
    const auto quotient=cert::over(numerator(f),det);
    if(!quotient.bounded())return {};
    return {clamp_energy(quotient,f.energy),2,true,true};
}
inline Interval explained(const FitIntervals& f) {
    if(!finite(f))return {-cert::infinity,cert::infinity};
    if(f.count_upper<=2 || f.energy.hi<=1e-30)return {};
    if(f.count_lower<=2)return {0,f.energy.hi};
    const auto product=cert::times(f.cc,f.ss),det=determinant(f);
    const auto cutoff=cert::times(Interval::point(1e-12),
        {std::max(1.,product.lo),std::max(1.,product.hi)});
    if(det.hi<=cutoff.lo)return {};
    if(f.energy.lo<=1e-30 || det.lo<=cutoff.hi)return {0,f.energy.hi};
    const auto quotient=cert::over(numerator(f),det);
    return quotient.bounded()?clamp_energy(quotient,f.energy):Interval{0,f.energy.hi};
}

struct Summary {
    // Original per-cell retain order; last cell is joined exactly once by
    // Direct::finish(). This also supports an explicit ORIGINAL carry seed.
    Interval projected;
    std::uint64_t rank=0,count=0;
    bool valid=true,nested=true;
    // Proves EVERY possibly nonempty original chip owner was represented,
    // including edge cells with no proxy observations. Count intervals alone
    // cannot establish this or fix a missing projected rank.
    bool owner_set_certified=false;
    void retain(const FitIntervals& cell) {
        if(!valid)return;
        const auto value=projection(cell);
        if(!value.valid || cell.count>std::numeric_limits<std::uint64_t>::max()-count ||
           value.rank>std::numeric_limits<std::uint64_t>::max()-rank) {valid=false;return;}
        projected=cert::plus(projected,value.energy);rank+=value.rank;count+=cell.count;
        nested=nested && value.nested;
        valid=projected.bounded();
    }
};
struct MathContract {
    // Qualified ORIGINAL -log1p(-f) absolute error; infinity means unknown.
    double log_error=cert::infinity;
    // ORIGINAL sqrt and this helper's sqrt are correctly rounded binary64.
    // The existing certificate environment check alone does not establish it.
    bool correctly_rounded_sqrt=false;
};

// Component bounds for proxy clocks: each certified chip boundary changes at
// most ONE observation's ownership. Both possible adjacent observations form
// a union; max, rather than their sum, bounds that one changed observation.
// Joining the left/right edge bounds allows +/-2 cell counts. The WHOLE count
// interval is derived independently from original usable-mask boundaries; the
// per-cell +/-2 values are never added to observed counts or whole count error.
struct FieldAllowance {
    double dot=0,gram=0,energy=0;
    std::uint64_t count=0;
    bool valid=true;
};
inline FieldAllowance sample_allowance(float pcm,std::complex<double> q,
        std::complex<double> oscillator,double template_radius,double carrier_radius) {
    if(!std::isfinite(pcm) || !cert::finite(q) || !cert::finite(oscillator) ||
       !std::isfinite(template_radius) || template_radius<0 ||
       !std::isfinite(carrier_radius) || carrier_radius<0)return {0,0,0,0,false};
    const auto basis=cert::mul(cert::add(cert::norm(q),template_radius),
        cert::add(cert::norm(oscillator),carrier_radius));
    const auto g=cert::gamma(16,std::numeric_limits<double>::epsilon()/2);
    const auto factor=cert::add(1,g),tiny=16*std::numeric_limits<double>::denorm_min();
    const auto x=std::abs(static_cast<double>(pcm));
    FieldAllowance result{cert::add(cert::mul(factor,cert::mul(x,basis)),tiny),
        cert::add(cert::mul(factor,cert::mul(basis,basis)),tiny),x*x,1,true};
    result.valid=std::isfinite(result.dot) && std::isfinite(result.gram) && std::isfinite(result.energy);
    return result;
}
inline FieldAllowance one_edge(FieldAllowance a,FieldAllowance b) {
    return {std::max(a.dot,b.dot),std::max(a.gram,b.gram),std::max(a.energy,b.energy),
        std::max(a.count,b.count),a.valid && b.valid};
}
inline FieldAllowance two_edges(FieldAllowance left,FieldAllowance right) {
    if(!left.valid || !right.valid || left.count>1 || right.count>1)return {0,0,0,0,false};
    return {cert::add(left.dot,right.dot),cert::add(left.gram,right.gram),
        cert::add(left.energy,right.energy),left.count+right.count,true};
}
inline FitIntervals membership_enclose(const CorrelationFit& proxy,
        const cert::ReferenceErrors& fields,FieldAllowance membership,bool exact_zero=false) {
    if(!fields.valid || fields.dot<0 || fields.gram<0 || fields.energy<0 ||
       !membership.valid || membership.dot<0 || membership.gram<0 || membership.energy<0 || membership.count>2)return {};
    return enclose(proxy,cert::add(fields.dot,membership.dot),cert::add(fields.gram,membership.gram),
        cert::add(fields.energy,membership.energy),exact_zero,membership.count);
}
struct OwnerRange {std::uint64_t lower=0,upper=0;bool valid=false;};
inline OwnerRange owner_range(Interval original_source,std::uint64_t chip,std::uint64_t symbol) {
    if(!chip || !symbol || chip>0x1fffffffffffffULL || symbol>0x1fffffffffffffULL ||
       !original_source.bounded() || original_source.lo<0 || original_source.hi>=symbol)return {};
    const auto divided=cert::over(original_source,Interval::point(static_cast<double>(chip)));
    if(!divided.bounded())return {};
    const auto final=(symbol-1)/chip;
    const auto lower=static_cast<std::uint64_t>(std::floor(std::max(0.,divided.lo)));
    const auto upper=std::min(final,static_cast<std::uint64_t>(std::floor(std::max(0.,divided.hi))));
    return {lower,upper,lower<=upper && upper-lower<=1};
}
inline Interval sqrt_interval(Interval a,const MathContract& math) {
    if(!math.correctly_rounded_sqrt || !a.bounded() || a.lo<0)return {-cert::infinity,cert::infinity};
    return {a.lo==0?0:cert::down(std::sqrt(a.lo)),cert::up(std::sqrt(a.hi))};
}
struct Guard {bool colored=false;double score_upper=cert::infinity;};
inline Guard guarded_upper(const FitIntervals& whole,const Summary& chips,double threshold,
                           const MathContract& math,double weak_energy_scale=1) {
    Guard result;
    if(!cert::environment<double>() || !finite(whole) || !chips.valid || !chips.nested || !chips.owner_set_certified ||
       !chips.projected.bounded() || chips.count!=whole.count ||
       !std::isfinite(threshold) || threshold<0 || !std::isfinite(weak_energy_scale) ||
       !math.correctly_rounded_sqrt || !std::isfinite(math.log_error) || math.log_error<0)return result;
    const auto coherent=projection(whole);
    if(!coherent.valid || chips.rank<=coherent.rank || chips.rank>=whole.count_lower ||
       whole.energy.lo<=1e-30 || chips.rank<=2 || chips.projected.lo<=1e-30)return result;
    const auto r=Interval::point(static_cast<double>(chips.rank-coherent.rank));
    const Interval d{static_cast<double>(whole.count_lower-chips.rank),
        static_cast<double>(whole.count_upper-chips.rank)};
    const auto delta=cert::times(cert::plus(cert::times(Interval::point(4),Interval::point(threshold)),
        Interval::point(32)),Interval::point(std::max(1.,weak_energy_scale)));
    const auto lower=cert::minus(d,cert::times(Interval::point(2),
        sqrt_interval(cert::times(d,Interval::point(30)),math)));
    if(!lower.bounded() || lower.lo<=0)return result;
    const auto upper=cert::plus(cert::plus(cert::plus(r,delta),cert::times(Interval::point(2),
        sqrt_interval(cert::times(cert::plus(r,cert::times(Interval::point(2),delta)),Interval::point(30)),math))),
        Interval::point(60));
    const auto ratio=cert::over(cert::over(upper,r),cert::over(lower,d));
    const Interval limit{std::max(2.,ratio.lo),std::max(2.,ratio.hi)};
    if(!ratio.bounded())return result;
    const auto projected=clamp_energy(chips.projected,whole.energy);
    const auto lack=cert::minus(projected,coherent.energy);
    const Interval lack_positive{std::max(0.,lack.lo),std::max(0.,lack.hi)};
    const auto residual=cert::minus(whole.energy,projected);
    const auto left=cert::times(lack_positive,d);
    const auto right=cert::times(cert::times(limit,residual),r);
    if(!left.bounded() || !right.bounded() || !(residual.hi<=0 || left.lo>right.hi))return result;
    result.colored=true;
    // chips.score uses whole.explained(), whose branch differs from projection.
    const auto exp=explained(whole);
    if(!exp.bounded())return result;
    const auto fraction=cert::over(exp,chips.projected);
    if(!fraction.bounded())return result;
    constexpr double cap=1.-1e-15;
    const auto f=std::clamp(fraction.hi,0.,cap);
    const auto log_upper=cert::add(cert::divide(f,cert::down(1-f)),math.log_error);
    // Interval multiplication encloses the final ORIGINAL rounded product.
    result.score_upper=cert::mul(.5*static_cast<double>(chips.rank-2),log_upper);
    return result;
}

// Direct bounded per-chip accumulation reuses already-shaped/clipped q rows
// and ORIGINAL physical carrier samples. No whole-H matrix or physical FFT.
// It gathers only admitted starts. Chip cells can cross null fragments, caller
// fences, and fragment boundaries; only a changed source owner flushes them.
// Every count/owner/mask MUST be certified against the ORIGINAL consumer first.
class Direct {
public:
    struct Lane {
        std::array<CorrelationFit,2> cell{};
        std::array<Summary,2> chips{};
        std::array<bool,2> zero{true,true};
        std::uint64_t owner=no_owner;
    };
    std::vector<Lane> lanes;
    std::uint64_t sample_pairs=0,cells=0;
    bool ownership_certified=false;
    bool exact_original_rows=false;
    std::uint64_t cell_count_radius=0;
    std::uint64_t next_fragment=0;
    bool initialized=false;
    bool finished=false;
    explicit Direct(std::size_t count,bool ownership,bool exact_rows=false,std::uint64_t count_radius=0,
                    bool original_owner_set=false)
        :lanes(count),ownership_certified(ownership),exact_original_rows(exact_rows),cell_count_radius(count_radius) {
        if(count>scheduled_acquisition_tile::maximum_start_span)throw Error("colored direct start bound exceeded");
        for(auto& lane:lanes)for(auto& summary:lane.chips)
            summary.owner_set_certified=ownership && (!count_radius || original_owner_set);
    }
    std::size_t working_bytes()const{return sizeof(*this)+lanes.capacity()*sizeof(Lane);}
    // original[i][bit] bounds that ORIGINAL cell's reduction/producer fields
    // against these same q/carrier rows. A conservative whole-span bound is
    // safe for each cell; unknown remains unknown. Native reduction error is
    // added here separately. Zero-basis proof requires exact ORIGINAL q reuse.
    using Errors=std::array<cert::ReferenceErrors,2>;
    void flush(std::size_t j,const Errors& original) {
        auto& lane=lanes[j];
        for(unsigned bit=0;bit<2;++bit) {
            auto& f=lane.cell[bit];if(!f.count)continue;
            if(f.count>(std::numeric_limits<std::size_t>::max()-32)/12) {
                lane.chips[bit].valid=false;f={};lane.zero[bit]=true;continue;
            }
            const auto native_gamma=cert::gamma(12*static_cast<std::size_t>(f.count)+32,
                std::numeric_limits<double>::epsilon()/2);
            const auto denominator=cert::down(1-native_gamma);
            const auto tiny=cert::mul(12*static_cast<double>(f.count)+32,
                std::numeric_limits<double>::denorm_min());
            const auto energy_upper=cert::divide(cert::add(std::max(0.,f.energy),tiny),denominator);
            const auto trace_upper=cert::divide(cert::add(cert::up(std::max(0.,f.cc+f.ss)),tiny),denominator);
            const auto local=cert::scalar_reference_errors(f.count,energy_upper,trace_upper,0);
            if(!original[bit].valid || original[bit].dot<0 || original[bit].gram<0 || original[bit].energy<0 ||
               !local.valid)lane.chips[bit].valid=false;
            else lane.chips[bit].retain(enclose(f,cert::add(local.dot,original[bit].dot),
                cert::add(local.gram,original[bit].gram),cert::add(local.energy,original[bit].energy),
                exact_original_rows && !cell_count_radius && lane.zero[bit],cell_count_radius));
            f={};lane.zero[bit]=true;
        }
        ++cells;
    }
    void add(const scheduled_acquisition_tile::Input& input,
             const scheduled_acquisition_tile::Fragment& fragment,
             std::span<const std::size_t> starts,std::span<const std::uint64_t> owners,
             std::span<const Errors> original,std::stop_token stop={}) {
        namespace tile=scheduled_acquisition_tile;
        if(finished)throw Error("colored direct cannot append after final cell");
        tile::validate(input,fragment,starts,stop);
        if(starts.size()!=lanes.size() || original.size()!=lanes.size() || owners.size()!=fragment.rows.size())
            throw Error("colored direct ownership extent mismatch");
        if(initialized && fragment.physical_offset<next_fragment)
            throw Error("colored direct physical fragments overlap or reverse");
        next_fragment=tile::add_coordinate(fragment.physical_offset,fragment.rows.size());initialized=true;
        if(!ownership_certified)return;
        for(std::size_t i=0;i<fragment.rows.size();++i) {
            if((i&255U)==0)tile::check_stop(stop);
            if(!tile::selected(fragment,i))continue;
            if(owners[i]==no_owner)throw Error("colored direct selected sample has no source owner");
            const auto q=fragment.rows[i];
            // Independent ascending lane stores admit compiler vectorization
            // across starts, including irregular starts. No reduction occurs
            // across candidates. The original operation association is covered
            // by explicit field radii, not assumed identical to this kernel.
            for(std::size_t j=0;j<lanes.size();++j) {
                const auto at=i+starts[j];if(!tile::present(input,at))continue;
                auto& lane=lanes[j];
                if(lane.owner!=owners[i]) {
                    if(lane.owner!=no_owner && owners[i]<lane.owner)throw Error("colored direct owners are not ascending");
                    flush(j,original[j]);lane.owner=owners[i];
                }
                const auto x=static_cast<double>(input.pcm[at]);const auto o=input.oscillator[at];
                for(unsigned bit=0;bit<2;++bit) {
                    const auto a=q[bit].real(),b=q[bit].imag();
                    const auto c=a*o.real()-b*o.imag(),s=b*o.real()+a*o.imag();
                    auto& f=lane.cell[bit];f.xc+=x*c;f.xs+=x*s;f.cc+=c*c;f.ss+=s*s;
                    if(f.count==std::numeric_limits<std::uint64_t>::max())throw Error("colored direct count overflow");
                    f.cs+=c*s;f.energy+=x*x;++f.count;lane.zero[bit]=lane.zero[bit] && a==0 && b==0;
                }
                ++sample_pairs;
            }
        }
    }
    std::vector<std::array<Summary,2>> finish(std::span<const Errors> original) {
        if(finished)throw Error("colored direct last cell already finalized");
        if(original.size()!=lanes.size())throw Error("colored direct final error extent mismatch");
        std::vector<std::array<Summary,2>> result;result.reserve(lanes.size());
        for(std::size_t j=0;j<lanes.size();++j){flush(j,original[j]);result.push_back(lanes[j].chips);
            if(!ownership_certified)for(auto& s:result.back())s.valid=false;}
        finished=true;return result;
    }
};

// Minimal rejection adapter: first prove the ORIGINAL colored predicate, then
// bound the ORIGINAL guarded score. Never propagate the colored predicate or
// summary as admission/absence evidence. A failure keeps the prior raw upper.
inline double rejection_upper(double raw_upper,const CorrelationFit& whole,
                              const cert::FitCertificate& tile,unsigned bit,
                              const cert::ReferenceErrors& original,const Summary& chips,
                              double threshold,const MathContract& math,std::uint64_t total_count_radius=0,
                              double weak_energy_scale=1) {
    if(bit>1 || !tile.valid || !original.valid || tile.dot_radius[bit]<0 ||
       tile.gram_radius[bit]<0 || original.dot<0 || original.gram<0 || original.energy<0)return raw_upper;
    auto enclosed=enclose(whole,cert::add(tile.dot_radius[bit],original.dot),
        cert::add(tile.gram_radius[bit],original.gram),original.energy,false,total_count_radius);
    enclosed.energy={std::max(0.,cert::down(tile.energy_lower-original.energy)),
        cert::up(tile.energy_upper+original.energy)};
    const auto result=guarded_upper(enclosed,chips,threshold,math,weak_energy_scale);
    return result.colored?std::min(raw_upper,result.score_upper):raw_upper;
}
} // namespace datapump::modem::detail::scheduled_cold_colored
