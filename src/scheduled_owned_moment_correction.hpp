#pragma once
// No PCM replay: correct already-certified mathematical native fields using
// only sparse, exact Original support edits. All storage belongs to the caller.
#include "scheduled_cold_colored_certificate.hpp"
#include <algorithm>
#include <array>
#include <complex>
#include <cstdint>
#include <span>

namespace datapump::modem::detail::scheduled_owned_moment_correction {
namespace cert=scheduled_acquisition_certificate;
namespace color=scheduled_cold_colored;
using Interval=cert::Interval;using Complex=std::complex<double>;
inline constexpr auto no_owner=color::no_owner;
inline constexpr std::uint64_t maximum_count=0x1fffffffffffffULL;
// Producer-issued, immutable identities. IDs must bind the full SourceRows key,
// pointer/mode/revision, candidate anchor/origin/rate, [B,F), translation,
// capture/frequency/physical carrier and Original processing-fence ledger.
// native_support is deliberately independent for WHOLE and chip producers.
struct Identity {
    std::uint64_t event=0,native_support=0,original_support=0,source_template=0,carrier=0,ledger=0;
    bool operator==(const Identity&)const=default;
    bool valid()const{return event && native_support && original_support && source_template && carrier && ledger;}
    bool same_original(const Identity& b)const {
        return valid() && b.valid() && event==b.event && original_support==b.original_support &&
            source_template==b.source_template && carrier==b.carrier && ledger==b.ledger;
    }
};
struct SupportProof {
    Identity identity;
    std::uint64_t native_count=0,original_count=0;
    // Trusted boundary producer proves exhaustive physical support, including
    // window/mask entrants, and reproduces Original PIECE anchoring exactly.
    bool complete_edits=false,complete_owners=false;
};
struct OwnerCount {std::uint64_t owner=no_owner,native_count=0,original_count=0;};
struct RowSample {
    float pcm=0;
    Complex oscillator{}; // Center enclosing the SAME raw carrier as moments.
    std::array<Complex,2> q0{}; // exact stored coefficients, not rounded q*o.
    std::array<double,2> original_q_radius{}; // optional external template proof.
    double oscillator_radius=0; // Euclidean raw-carrier disk; trailing0 preserves exact aggregates.
};
struct RowEdit {
    Identity identity;
    std::uint64_t physical=0,native_owner=no_owner,original_owner=no_owner;
    RowSample row;
};
struct Fields {
    Interval xc,xs,cc,ss,cs,energy;
    bool bounded()const{return xc.bounded() && xs.bounded() && cc.bounded() &&
        ss.bounded() && cs.bounded() && energy.bounded();}
};
inline bool zero(Interval x){return x.lo==0 && x.hi==0;}
inline Interval add(Interval a,Interval b){return zero(a)?b:(zero(b)?a:cert::plus(a,b));}
inline Interval subtract(Interval a,Interval b){return zero(b)?a:(zero(a)?cert::negate(b):cert::minus(a,b));}
inline Interval multiply(Interval a,Interval b){return zero(a)||zero(b)?Interval{}:cert::times(a,b);}
inline bool nonnegative(double x){return std::isfinite(x) && x>=0;}
inline bool valid_errors(const cert::ReferenceErrors& e) {
    return e.valid && nonnegative(e.dot) && nonnegative(e.gram) && nonnegative(e.energy);
}
inline Fields fields(const color::FitIntervals& f){return {f.xc,f.xs,f.cc,f.ss,f.cs,f.energy};}
inline Fields combine(Fields a,const Fields& b,bool plus) {
    const auto op=plus?add:subtract;
    return {op(a.xc,b.xc),op(a.xs,b.xs),op(a.cc,b.cc),op(a.ss,b.ss),op(a.cs,b.cs),op(a.energy,b.energy)};
}
// Encloses mathematical REAL products. Original CorrelationProjection::add
// association and floating-point accumulation are attached ONLY at finalize.
inline bool row_fields(const RowSample& row,std::array<Fields,2>& result) {
    if(!cert::environment<double>() || !std::isfinite(row.pcm) || !cert::finite(row.oscillator) ||
       !nonnegative(row.oscillator_radius))return false;
    const auto x=Interval::point(static_cast<double>(row.pcm));
    // Radius0 follows the previous exact stored-carrier path byte for byte.
    const auto real=row.oscillator_radius==0?Interval::point(row.oscillator.real()):
        Interval::disk(row.oscillator.real(),row.oscillator_radius);
    const auto imag=row.oscillator_radius==0?Interval::point(row.oscillator.imag()):
        Interval::disk(row.oscillator.imag(),row.oscillator_radius);
    if(!real.bounded() || !imag.bounded())return false;
    for(unsigned bit=0;bit<2;++bit) {
        if(!cert::finite(row.q0[bit]) || !nonnegative(row.original_q_radius[bit]))return false;
        const auto a=Interval::point(row.q0[bit].real()),b=Interval::point(row.q0[bit].imag());
        const auto c=subtract(multiply(a,real),multiply(b,imag));
        const auto s=add(multiply(b,real),multiply(a,imag));
        result[bit]={multiply(x,c),multiply(x,s),cert::square(c),cert::square(s),multiply(c,s),cert::pcm_square(row.pcm)};
        if(!result[bit].bounded())return false;
    }
    return true;
}
// Convenience for a caller's final corrected-support template error producer.
// Add this ONCE for every selected Original edited row whose uncertainty is not
// already in the supplied bulk error. It is never subtracted from base errors.
inline std::array<cert::ReferenceErrors,2> row_template_errors(const RowSample& row) {
    std::array<cert::ReferenceErrors,2> result{};std::array<Fields,2> unused;
    if(!row_fields(row,unused))return result;
    const auto magnitude=cert::add(cert::norm(row.oscillator),row.oscillator_radius),absolute_x=std::abs(static_cast<double>(row.pcm));
    for(unsigned bit=0;bit<2;++bit) {
        const auto delta=cert::mul(magnitude,row.original_q_radius[bit]);
        const auto q=cert::mul(magnitude,cert::norm(row.q0[bit]));
        result[bit]={cert::mul(absolute_x,delta),cert::add(cert::mul(cert::mul(2,q),delta),cert::mul(delta,delta)),0,0,true};
        if(!valid_errors(result[bit]))return {};
    }
    return result;
}
struct OwnerDelta {
    Identity identity;OwnerCount count;
    std::array<Fields,2> delta{};
    std::uint64_t removed=0,added=0;
    bool valid=false;
};
inline bool count_changed(std::uint64_t native,std::uint64_t removed,std::uint64_t added,std::uint64_t original) {
    return native<=maximum_count && original<=maximum_count && removed<=native &&
        added<=maximum_count-(native-removed) && native-removed+added==original;
}
inline bool proof_valid(const SupportProof& proof) {
    return cert::environment<double>() && proof.identity.valid() && proof.complete_edits &&
        proof.native_count<=maximum_count && proof.original_count<=maximum_count;
}
// Complete owners, including native0 entrants and original0 departed owners,
// are sorted by ID. Edits are strictly sorted by physical row (dedup required).
// Output is provisional until true; no generated/allocated storage is hidden.
inline bool build_owner_deltas(const SupportProof& proof,std::span<const OwnerCount> owners,
        std::span<const RowEdit> edits,std::span<OwnerDelta> output) {
    for(auto& out:output)out.valid=false;
    if(!proof_valid(proof) || !proof.complete_owners || owners.size()!=output.size())return false;
    std::uint64_t native=0,original=0;
    for(std::size_t i=0;i<owners.size();++i) {
        const auto& o=owners[i];
        if(o.owner==no_owner || (i && o.owner<=owners[i-1].owner) || (!o.native_count && !o.original_count) ||
           o.native_count>maximum_count-native || o.original_count>maximum_count-original)return false;
        native+=o.native_count;original+=o.original_count;
        output[i]={proof.identity,o,{},0,0,false};
    }
    if(native!=proof.native_count || original!=proof.original_count)return false;
    const auto locate=[&](std::uint64_t owner)->OwnerDelta* {
        const auto it=std::lower_bound(output.begin(),output.end(),owner,[](const auto& a,auto id){return a.count.owner<id;});
        return it!=output.end() && it->count.owner==owner?&*it:nullptr;
    };
    for(std::size_t i=0;i<edits.size();++i) {
        const auto& e=edits[i];std::array<Fields,2> f;
        if(e.identity!=proof.identity || (i && e.physical<=edits[i-1].physical) ||
           e.native_owner==e.original_owner || !row_fields(e.row,f))return false;
        for(unsigned side=0;side<2;++side) {
            const auto owner=side?e.original_owner:e.native_owner;if(owner==no_owner)continue;
            auto* out=locate(owner);if(!out)return false;
            auto& count=side?out->added:out->removed;if(count==maximum_count)return false;++count;
            for(unsigned bit=0;bit<2;++bit){out->delta[bit]=combine(out->delta[bit],f[bit],side!=0);if(!out->delta[bit].bounded())return false;}
        }
    }
    for(const auto& out:output)if(!count_changed(out.count.native_count,out.removed,out.added,out.count.original_count))return false;
    for(auto& out:output)out.valid=true;
    return true;
}
struct WholeDelta {
    Identity identity;
    std::array<Fields,2> delta{};
    std::uint64_t native_count=0,original_count=0,removed=0,added=0;
    bool valid=false;
};
// WHOLE has its OWN support identity and own complete edit proof. A chip-only
// transfer contributes zero here. Do not reuse a screen tile as this base unless
// the producer independently proved that exact native_support identity.
inline bool build_whole_delta(const SupportProof& proof,std::span<const RowEdit> edits,WholeDelta& out) {
    out={};if(!proof_valid(proof))return false;
    out.identity=proof.identity;out.native_count=proof.native_count;out.original_count=proof.original_count;
    for(std::size_t i=0;i<edits.size();++i) {
        const auto& e=edits[i];std::array<Fields,2> f;
        if(e.identity!=proof.identity || (i && e.physical<=edits[i-1].physical) ||
           e.native_owner==e.original_owner || !row_fields(e.row,f))return false;
        const bool native=e.native_owner!=no_owner,original=e.original_owner!=no_owner;
        if(native==original)continue;
        auto& count=original?out.added:out.removed;if(count==maximum_count)return false;++count;
        for(unsigned bit=0;bit<2;++bit){out.delta[bit]=combine(out.delta[bit],f[bit],original);if(!out.delta[bit].bounded())return false;}
    }
    out.valid=count_changed(out.native_count,out.removed,out.added,out.original_count);return out.valid;
}
struct NativeCell {
    Identity identity;std::uint64_t owner=no_owner;
    std::array<color::FitIntervals,2> fields{};
};
// Complete proof of empty native support. Does NOT assert Original zero basis.
inline NativeCell empty_native(Identity identity,std::uint64_t owner) {
    NativeCell result{identity,owner,{}};for(auto& f:result.fields)f.valid=true;return result;
}
struct FinalErrors {
    Identity identity;std::uint64_t owner=no_owner,original_count=0;
    // Complete nonlocal template/carrier/Original-prefix/reduction radii on the
    // CORRECTED selected set. They must enclose mathematical Original row sums
    // too. Native moment/FFT error belongs in NativeCell, never again here.
    // finalize additionally adds corrected-count Original scalar error itself.
    std::array<cert::ReferenceErrors,2> fields{};
    std::array<bool,2> original_zero{}; // independently proved on EVERY Original row.
    bool complete=false;
};
struct Result {std::array<color::FitIntervals,2> fields{};bool valid=false;};
inline bool exact_count(const color::FitIntervals& f,std::uint64_t count) {
    return color::finite(f) && f.count==count && f.count_lower==count && f.count_upper==count;
}
inline bool empty_fields(const color::FitIntervals& f) {
    return zero(f.xc)&&zero(f.xs)&&zero(f.cc)&&zero(f.ss)&&zero(f.cs)&&zero(f.energy);
}
inline Interval widen(Interval x,double radius){return radius==0?x:add(x,Interval::disk(0,radius));}
// No Original errors yet. These fields may be combined into one independently
// stamped whole mathematical fit; never project/sum chip energies for that fit.
struct MathCell {
    Identity identity;std::uint64_t owner=no_owner;
    std::array<color::FitIntervals,2> fields{};
    bool valid=false;
};
inline MathCell correct_math(const NativeCell& native,const Identity& identity,std::uint64_t owner,
        std::uint64_t native_count,std::uint64_t original_count,const std::array<Fields,2>& delta) {
    MathCell result{identity,owner,{}};
    if(!cert::environment<double>() || !identity.valid() || native.identity!=identity || native.owner!=owner ||
       native_count>maximum_count || original_count>maximum_count)return result;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& base=native.fields[bit];
        if(!exact_count(base,native_count) || (!native_count && !empty_fields(base)) || !delta[bit].bounded())return result;
        const auto math=combine(fields(base),delta[bit],true);
        if(!math.bounded() || math.energy.hi<0 || math.cc.hi<0 || math.ss.hi<0)return result;
        auto& f=result.fields[bit];
        // Exhaustive Original empty support proves mathematical empty sums.
        if(!original_count){f.valid=true;continue;}
        f={math.xc,math.xs,math.cc,math.ss,math.cs,math.energy,original_count,original_count,original_count,true,false};
        f.energy.lo=std::max(0.,f.energy.lo);f.cc.lo=std::max(0.,f.cc.lo);f.ss.lo=std::max(0.,f.ss.lo);
        if(!color::finite(f))return result;
    }
    result.valid=cert::environment<double>();return result;
}
inline MathCell corrected_math_cell(const NativeCell& native,const OwnerDelta& delta) {
    if(!delta.valid)return {};
    return correct_math(native,delta.identity,delta.count.owner,delta.count.native_count,delta.count.original_count,delta.delta);
}
inline MathCell corrected_math_whole(const NativeCell& native,const WholeDelta& delta) {
    if(!delta.valid || native.owner!=no_owner)return {};
    return correct_math(native,delta.identity,no_owner,delta.native_count,delta.original_count,delta.delta);
}
inline MathCell combine_math_whole(const SupportProof& complete_partition,std::span<const MathCell> cells) {
    MathCell result{complete_partition.identity,no_owner,{}};
    if(!proof_valid(complete_partition) || !complete_partition.complete_owners)return result;
    for(auto& f:result.fields)f.valid=true;
    std::uint64_t count=0,previous=no_owner;
    for(const auto& cell:cells) {
        if(!cell.valid || !cell.identity.same_original(result.identity) || cell.owner==no_owner ||
           (previous!=no_owner && cell.owner<=previous))return result;
        previous=cell.owner;const auto n=cell.fields[0].count;
        if(n>maximum_count-count)return result;count+=n;
        for(unsigned bit=0;bit<2;++bit) {
            if(!exact_count(cell.fields[bit],n))return result;
            const auto joined=combine(fields(result.fields[bit]),fields(cell.fields[bit]),true);
            auto& f=result.fields[bit];
            f={joined.xc,joined.xs,joined.cc,joined.ss,joined.cs,joined.energy,count,count,count,true,false};
            if(!color::finite(f))return result;
        }
    }
    result.valid=count==complete_partition.original_count && cert::environment<double>();return result;
}
// Bounds mathematical Original rows and the independently executed Original
// cell/whole prefix evaluation. Native scalar slack (if any) stays conservative;
// this new scalar allowance uses the corrected count and corrected caps.
inline Result originalize(const MathCell& math,const FinalErrors& errors) {
    Result result;
    if(!math.valid || !cert::environment<double>() || !math.identity.valid() || !errors.complete ||
       errors.identity!=math.identity || errors.owner!=math.owner || errors.original_count>maximum_count)return result;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& base=math.fields[bit];const auto& error=errors.fields[bit];
        if(!exact_count(base,errors.original_count) || !valid_errors(error))return result;
        auto& f=result.fields[bit];
        if(!errors.original_count){f.valid=f.zero_basis=true;continue;}
        f=base;f.zero_basis=false;
        f.xc=widen(f.xc,error.dot);f.xs=widen(f.xs,error.dot);
        f.cc=widen(f.cc,error.gram);f.ss=widen(f.ss,error.gram);f.cs=widen(f.cs,error.gram);
        f.energy=widen(f.energy,error.energy);f.energy.lo=std::max(0.,f.energy.lo);
        if(!color::finite(f) || f.cc.hi<0 || f.ss.hi<0)return result;
        const auto trace=add(f.cc,f.ss);
        if(!trace.bounded() || trace.hi<0)return result;
        const auto scalar=cert::scalar_reference_errors(errors.original_count,f.energy.hi,trace.hi,0);
        if(!valid_errors(scalar))return result;
        f.xc=widen(f.xc,scalar.dot);f.xs=widen(f.xs,scalar.dot);
        f.cc=widen(f.cc,scalar.gram);f.ss=widen(f.ss,scalar.gram);f.cs=widen(f.cs,scalar.gram);
        f.energy=widen(f.energy,scalar.energy);f.energy.lo=std::max(0.,f.energy.lo);
        if(errors.original_zero[bit]){f.xc=f.xs=f.cc=f.ss=f.cs={};f.zero_basis=true;}
        if(!color::finite(f))return result;
    }
    result.valid=cert::environment<double>();return result;
}
inline Result corrected_cell(const NativeCell& native,const OwnerDelta& delta,const FinalErrors& errors) {
    if(!delta.valid)return {};
    return originalize(corrected_math_cell(native,delta),errors);
}
inline Result corrected_whole(const NativeCell& native,const WholeDelta& delta,const FinalErrors& errors) {
    if(!delta.valid || native.owner!=no_owner)return {};
    return originalize(corrected_math_whole(native,delta),errors);
}
} // namespace datapump::modem::detail::scheduled_owned_moment_correction
