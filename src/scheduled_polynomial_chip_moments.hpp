#pragma once
// Artifact-only C34 full-cell producer. Caller owns exact same-owner, selected,
// present, contiguous physical rows and Original observation-domain proof.
#include "scheduled_cold_colored_certificate.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace datapump::modem::detail::scheduled_polynomial_chip {
namespace cert=scheduled_acquisition_certificate;
namespace color=scheduled_cold_colored;
using Complex=std::complex<double>;using Interval=cert::Interval;
inline constexpr std::size_t cell_samples=34,degree=3,maximum_capacity=32768;
inline constexpr std::size_t helper_reservation=8192;
inline void check_stop(std::stop_token stop){if(stop.stop_requested())throw Error("polynomial chip cancelled");}
inline bool zero(Interval a){return a.lo==0 && a.hi==0;}
inline bool nonnegative(double a){return std::isfinite(a) && a>=0;}
inline Interval interval_plus(Interval a,Interval b){return zero(a)?b:(zero(b)?a:cert::plus(a,b));}
inline Interval interval_times(Interval a,Interval b) {
    if(zero(a) || zero(b))return {};
    if(a.lo==1 && a.hi==1)return b;if(b.lo==1 && b.hi==1)return a;
    if(a.lo==-1 && a.hi==-1)return cert::negate(b);if(b.lo==-1 && b.hi==-1)return cert::negate(a);
    return cert::times(a,b);
}
inline Interval interval_minus(Interval a,Interval b){return zero(b)?a:cert::minus(a,b);}
inline Interval widen(Interval a,double e){return e==0?a:cert::plus(a,Interval::disk(0,e));}
inline std::size_t add_size(std::size_t a,std::size_t b) {
    if(b>std::numeric_limits<std::size_t>::max()-a)throw Error("polynomial chip size overflow");return a+b;
}
inline std::size_t mul_size(std::size_t a,std::size_t b) {
    if(a && b>std::numeric_limits<std::size_t>::max()/a)throw Error("polynomial chip size overflow");return a*b;
}
struct BoundedReal {double value=0;Interval interval;};
struct BoundedComplex {Complex value{};Interval real,imag;};
inline BoundedComplex point(Complex value){return {value,Interval::point(value.real()),Interval::point(value.imag())};}
inline BoundedComplex add(BoundedComplex a,BoundedComplex b) {
    return {a.value+b.value,interval_plus(a.real,b.real),interval_plus(a.imag,b.imag)};
}
inline BoundedComplex multiply(BoundedComplex a,BoundedComplex b) {
    return {a.value*b.value,interval_minus(interval_times(a.real,b.real),interval_times(a.imag,b.imag)),
        interval_plus(interval_times(a.real,b.imag),interval_times(a.imag,b.real))};
}
inline BoundedComplex multiply(BoundedComplex a,BoundedReal b) {
    return {a.value*b.value,interval_times(a.real,b.interval),interval_times(a.imag,b.interval)};
}
struct Basis {
    std::array<double,cell_samples> t{};
    std::array<std::array<Interval,2*degree+1>,cell_samples> powers{};
    Basis() {
        for(std::size_t n=0;n<cell_samples;++n) {
            // Basis points are these EXACT stored FP64 values. Mathematical
            // basis functions are exact-real powers, not rounded power arrays.
            t[n]=static_cast<double>(2*static_cast<int>(n)-33)/33;
            powers[n][0]=Interval::point(1);
            for(std::size_t k=1;k<=2*degree;++k)powers[n][k]=interval_times(powers[n][k-1],Interval::point(t[n]));
        }
    }
};
inline const Basis& basis() {
    // Shared public table, no per-thread TLS or symbol-sized allocation. A
    // conservative full table allowance is included in cache helper reservation.
    static const Basis value;return value;
}
struct SourceCell {
    std::array<std::array<Complex,degree+1>,2> coefficients{};
    std::array<std::array<BoundedComplex,2*degree+1>,2> image_coefficients{};
    std::array<std::array<BoundedReal,2*degree+1>,2> trace_coefficients{};
    std::array<double,2> residual_square{},polynomial_square{};
    std::array<bool,2> original_zero{};
    bool valid=false;
    std::size_t working_bytes()const{return sizeof(*this);}
};
static_assert(std::is_trivially_copyable_v<SourceCell>);

inline SourceCell build_source(std::span<const std::array<Complex,2>> rows,std::stop_token stop={}) {
    if(rows.size()!=cell_samples || !cert::environment<double>())throw Error("polynomial chip needs full C34 source cell");
    check_stop(stop);const auto& table=basis();SourceCell result;
    for(const auto& row:rows)for(const auto q:row)if(!cert::finite(q))throw Error("nonfinite polynomial chip source");
    // Stable four-node interpolation supplies arbitrary finite stored cubic
    // coefficients. No approximation theorem is assumed: every residual below
    // is subsequently measured and enclosed against ALL34 actual stored rows.
    constexpr std::array<std::size_t,4> nodes{0,11,22,33};
    for(unsigned bit=0;bit<2;++bit) {
        result.original_zero[bit]=std::all_of(rows.begin(),rows.end(),[&](const auto& q){return q[bit]==Complex{};});
        if(result.original_zero[bit])continue;
        for(std::size_t node=0;node<nodes.size();++node) {
            std::array<double,4> polynomial{1,0,0,0};std::size_t order=0;double denominator=1;
            for(std::size_t other=0;other<nodes.size();++other)if(node!=other) {
                std::array<double,4> next{};
                for(std::size_t k=0;k<=order;++k){next[k]-=table.t[nodes[other]]*polynomial[k];next[k+1]+=polynomial[k];}
                polynomial=next;++order;denominator*=table.t[nodes[node]]-table.t[nodes[other]];
            }
            if(!std::isfinite(denominator) || denominator==0)throw Error("polynomial chip interpolation singular");
            for(std::size_t k=0;k<=degree;++k)result.coefficients[bit][k]+=rows[nodes[node]][bit]*(polynomial[k]/denominator);
        }
        for(const auto a:result.coefficients[bit])if(!cert::finite(a))throw Error("polynomial chip coefficient overflow");
        for(std::size_t n=0;n<cell_samples;++n) {
            check_stop(stop);Interval real{},imag{};
            for(std::size_t k=0;k<=degree;++k) {
                real=interval_plus(real,interval_times(Interval::point(result.coefficients[bit][k].real()),table.powers[n][k]));
                imag=interval_plus(imag,interval_times(Interval::point(result.coefficients[bit][k].imag()),table.powers[n][k]));
            }
            const auto er=interval_minus(Interval::point(rows[n][bit].real()),real),ei=interval_minus(Interval::point(rows[n][bit].imag()),imag);
            const auto residual=interval_plus(cert::square(er),cert::square(ei));
            const auto norm=interval_plus(cert::square(real),cert::square(imag));
            result.residual_square[bit]=cert::add(result.residual_square[bit],residual.hi);
            result.polynomial_square[bit]=cert::add(result.polynomial_square[bit],norm.hi);
        }
        for(std::size_t i=0;i<=degree;++i)for(std::size_t j=0;j<=degree;++j) {
            const auto a=point(result.coefficients[bit][i]),b=point(result.coefficients[bit][j]);
            auto& image=result.image_coefficients[bit][i+j];image=add(image,multiply(a,b));
            auto& trace=result.trace_coefficients[bit][i+j];
            trace.value+=a.value.real()*b.value.real()+a.value.imag()*b.value.imag();
            trace.interval=interval_plus(trace.interval,interval_plus(interval_times(a.real,b.real),interval_times(a.imag,b.imag)));
        }
        if(!nonnegative(result.residual_square[bit]) || !nonnegative(result.polynomial_square[bit]))
            throw Error("polynomial chip residual overflow");
    }
    if(!cert::environment<double>())throw Error("polynomial chip source environment changed");
    result.valid=true;return result;
}

struct Moments {
    std::array<BoundedComplex,degree+1> dot{};
    std::array<BoundedComplex,2*degree+1> image{};
    std::array<BoundedReal,2*degree+1> trace{};
    BoundedReal energy;
    double carrier_square_upper=0;
    std::uint64_t count=0;
    bool valid=false;
};
static_assert(std::is_trivially_copyable_v<Moments>);

// Validation of a TRUSTED complete numerical producer, not a proof of its
// physical provenance. Intervals enclose mathematical targets; native centers
// need not lie inside those intervals. OriginalReferenceErrors remain separate.
inline bool valid_moments(const Moments& m) {
    if(!m.valid || m.count!=cell_samples || !cert::environment<double>() ||
       !nonnegative(m.energy.value) || !m.energy.interval.bounded() || m.energy.interval.lo<0 ||
       !nonnegative(m.carrier_square_upper))return false;
    for(const auto& v:m.dot)if(!cert::finite(v.value) || !v.real.bounded() || !v.imag.bounded())return false;
    for(const auto& v:m.image)if(!cert::finite(v.value) || !v.real.bounded() || !v.imag.bounded())return false;
    for(const auto& v:m.trace)if(!std::isfinite(v.value) || !v.interval.bounded())return false;
    return true;
}

inline Moments build_moments(std::span<const float> pcm,std::span<const Complex> oscillator,std::stop_token stop={}) {
    if(pcm.size()!=cell_samples || oscillator.size()!=cell_samples || !cert::environment<double>())
        throw Error("polynomial chip needs full C34 physical cell");
    check_stop(stop);const auto& table=basis();Moments result;
    for(std::size_t n=0;n<cell_samples;++n) {
        check_stop(stop);const auto x=static_cast<double>(pcm[n]);const auto o=oscillator[n];
        if(!std::isfinite(x) || !cert::finite(o))throw Error("nonfinite polynomial chip observation");
        const auto z=point(o),square=multiply(z,z);
        const auto norm=interval_plus(interval_times(z.real,z.real),interval_times(z.imag,z.imag));
        result.carrier_square_upper=std::max(result.carrier_square_upper,norm.hi);
        double power=1;
        for(std::size_t k=0;k<=2*degree;++k) {
            const BoundedReal p{power,table.powers[n][k]};
            if(k<=degree)result.dot[k]=add(result.dot[k],multiply(multiply(point({x,0}),z),p));
            result.image[k]=add(result.image[k],multiply(square,p));
            result.trace[k].value+=std::norm(o)*power;
            result.trace[k].interval=interval_plus(result.trace[k].interval,interval_times(norm,p.interval));
            power*=table.t[n];
        }
        result.energy.value+=x*x;
        result.energy.interval=interval_plus(result.energy.interval,cert::pcm_square(pcm[n]));++result.count;
    }
    if(!result.energy.interval.bounded() || !nonnegative(result.carrier_square_upper) ||
       !cert::environment<double>())throw Error("polynomial chip moment environment or range failure");
    for(const auto& v:result.dot)if(!cert::finite(v.value) || !v.real.bounded() || !v.imag.bounded())throw Error("polynomial chip dot moment overflow");
    for(const auto& v:result.image)if(!cert::finite(v.value) || !v.real.bounded() || !v.imag.bounded())throw Error("polynomial chip image moment overflow");
    for(const auto& v:result.trace)if(!std::isfinite(v.value) || !v.interval.bounded())throw Error("polynomial chip trace moment overflow");
    result.valid=true;return result;
}
struct CellResult {
    std::array<CorrelationFit,2> fits{};
    std::array<color::FitIntervals,2> intervals{};
    bool supported=false;
};
inline CellResult contract_impl(const SourceCell& source,const Moments& moments,
                          const std::array<cert::ReferenceErrors,2>& original,bool original_scalar) {
    CellResult result;
    if(!source.valid || !moments.valid || moments.count!=cell_samples || !cert::environment<double>())return result;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& reference=original[bit];
        if(!reference.valid || !nonnegative(reference.dot) || !nonnegative(reference.gram) || !nonnegative(reference.energy))return result;
        BoundedComplex dot{},image{};BoundedReal trace;
        for(std::size_t k=0;k<=degree;++k)dot=add(dot,multiply(point(source.coefficients[bit][k]),moments.dot[k]));
        for(std::size_t k=0;k<=2*degree;++k) {
            image=add(image,multiply(source.image_coefficients[bit][k],moments.image[k]));
            trace.value+=source.trace_coefficients[bit][k].value*moments.trace[k].value;
            trace.interval=interval_plus(trace.interval,interval_times(source.trace_coefficients[bit][k].interval,moments.trace[k].interval));
        }
        if(!trace.interval.bounded() || trace.interval.hi<0)return result;
        auto& fit=result.fits[bit];fit.xc=dot.value.real();fit.xs=dot.value.imag();
        fit.cc=.5*(trace.value+image.value.real());fit.ss=.5*(trace.value-image.value.real());fit.cs=.5*image.value.imag();
        fit.energy=moments.energy.value;fit.count=cell_samples;
        const auto residual=source.residual_square[bit],polynomial=source.polynomial_square[bit];
        const auto w=cert::mul(moments.carrier_square_upper,residual);
        const auto approximation_dot=cert::root(cert::mul(std::max(0.,moments.energy.interval.hi),w));
        const auto approximation_gram=cert::mul(moments.carrier_square_upper,
            cert::add(cert::mul(2,cert::root(cert::mul(polynomial,residual))),residual));
        const auto qnorm=cert::add(cert::root(polynomial),cert::root(residual));
        const auto original_trace=cert::mul(moments.carrier_square_upper,cert::mul(qnorm,qnorm));
        // Outward moments enclose exact-real stored-row algebra. Executed
        // ORIGINAL per-sample products/reduction require this separate scalar
        // allowance, in addition to caller producer/prefix/proxy errors.
        const auto scalar=original_scalar?cert::scalar_reference_errors(cell_samples,
            std::max(0.,moments.energy.interval.hi),original_trace,0):cert::ReferenceErrors{0,0,0,0,true};
        if(!scalar.valid)return result;
        const auto dot_radius=cert::add(approximation_dot,cert::add(reference.dot,scalar.dot));
        const auto gram_radius=cert::add(approximation_gram,cert::add(reference.gram,scalar.gram));
        const auto energy_radius=cert::add(reference.energy,scalar.energy);
        auto& interval=result.intervals[bit];
        interval.xc=widen(dot.real,dot_radius);interval.xs=widen(dot.imag,dot_radius);
        interval.cc=widen(interval_times(Interval::point(.5),interval_plus(trace.interval,image.real)),gram_radius);
        interval.ss=widen(interval_times(Interval::point(.5),interval_minus(trace.interval,image.real)),gram_radius);
        interval.cs=widen(interval_times(Interval::point(.5),image.imag),gram_radius);
        interval.energy=widen(moments.energy.interval,energy_radius);interval.energy.lo=std::max(0.,interval.energy.lo);
        interval.count=interval.count_lower=interval.count_upper=cell_samples;interval.valid=true;
        // Exactly zero stored q AND zero caller template/producer field error
        // proves original zero basis. Zero cubic with nonzero residual does not.
        if(source.original_zero[bit] && reference.dot==0 && reference.gram==0) {
            interval.xc=interval.xs=interval.cc=interval.ss=interval.cs={};interval.zero_basis=true;
        }
        if(!color::finite(interval) || !cert::finite({fit.xc,fit.xs}) || !std::isfinite(fit.cc) ||
           !std::isfinite(fit.ss) || !std::isfinite(fit.cs) || !std::isfinite(fit.energy))return result;
    }
    result.supported=cert::environment<double>();return result;
}


// Mathematical stored-q0 / stored-physical-carrier sums only. This entry point
// retains interval moment arithmetic and ALL measured q0-to-cubic residuals;
// it omits Original execution/prefix/template allowances. Its output may be
// support-corrected before one final corrected-count Original allowance.
inline CellResult contract_math(const SourceCell& source,const Moments& moments) {
    const std::array<cert::ReferenceErrors,2> zero{{{0,0,0,0,true},{0,0,0,0,true}}};
    return contract_impl(source,moments,zero,false);
}
inline CellResult contract(const SourceCell& source,const Moments& moments,
                          const std::array<cert::ReferenceErrors,2>& original) {
    return contract_impl(source,moments,original,true);
}

struct PhysicalKey {
    std::uint64_t capture=0,carrier=0,fence=0,frequency=0,physical_first=0,layout=0,screen_capture_end=0;
    bool operator==(const PhysicalKey&)const=default;
};
class MomentCache {
public:
    struct Counters {
        std::uint64_t queries=0,hits=0,builds=0,evictions=0,clears=0,rehashes=0,probe_steps=0,physical_sample_rows=0,certified_builds=0;
        bool saturated=false;
    };
private:
    static constexpr auto empty=std::numeric_limits<std::size_t>::max(),tombstone=empty-1;
    struct Entry {PhysicalKey key;Moments moments;std::size_t previous=empty,next=empty,slot=empty;bool valid=false;};
    std::vector<Entry> entries_;
    std::vector<std::size_t> slots_;
    std::size_t limit_=0,used_=0,head_=empty,tail_=empty,tombstones_=0;
    Counters counters_;
    void increment(std::uint64_t& value,std::uint64_t n=1) {
        if(n>std::numeric_limits<std::uint64_t>::max()-value){value=std::numeric_limits<std::uint64_t>::max();counters_.saturated=true;}
        else value+=n;
    }
    static std::uint64_t mix(std::uint64_t x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
    static std::size_t hash(const PhysicalKey& k) {
        std::uint64_t h=0x9e3779b97f4a7c15ULL;
        for(const auto value:{k.capture,k.carrier,k.fence,k.frequency,k.physical_first,k.layout,k.screen_capture_end})h=mix(h^mix(value));
        return static_cast<std::size_t>(h);
    }
    static void validate_key(const PhysicalKey& k) {
        if(!k.capture || !k.carrier || !k.fence || !k.frequency || !k.layout ||
           k.physical_first>std::numeric_limits<std::uint64_t>::max()-cell_samples ||
           k.screen_capture_end<k.physical_first+cell_samples)throw Error("polynomial chip physical identity incomplete");
    }
    std::size_t position(const PhysicalKey& key,std::stop_token stop={}) {
        const auto mask=slots_.size()-1;auto at=hash(key)&mask,first_tombstone=empty;
        for(std::size_t visited=0;visited<slots_.size();++visited,at=(at+1)&mask) {
            if((visited&255U)==0)check_stop(stop);increment(counters_.probe_steps);
            const auto index=slots_[at];
            if(index==empty)return first_tombstone==empty?at:first_tombstone;
            if(index==tombstone){if(first_tombstone==empty)first_tombstone=at;continue;}
            if(entries_[index].key==key)return at;
        }
        if(first_tombstone!=empty)return first_tombstone;
        throw Error("polynomial chip cache hash saturated");
    }
    void unlink(std::size_t i) {
        auto& e=entries_[i];if(e.previous!=empty)entries_[e.previous].next=e.next;else head_=e.next;
        if(e.next!=empty)entries_[e.next].previous=e.previous;else tail_=e.previous;
    }
    void touch(std::size_t i) {
        auto& e=entries_[i];if(head_==i)return;if(e.valid)unlink(i);
        e.previous=empty;e.next=head_;if(head_!=empty)entries_[head_].previous=i;else tail_=i;head_=i;
    }
    void rehash() {
        std::fill(slots_.begin(),slots_.end(),empty);tombstones_=0;
        for(std::size_t i=0;i<used_;++i)if(entries_[i].valid) {
            auto at=hash(entries_[i].key)&(slots_.size()-1);while(slots_[at]!=empty)at=(at+1)&(slots_.size()-1);
            slots_[at]=i;entries_[i].slot=at;
        }
        increment(counters_.rehashes);
    }
public:
    static std::size_t reservation(std::size_t capacity=8192) {
        if(capacity>maximum_capacity)throw Error("polynomial chip cache capacity exceeds bound");
        const auto slots=capacity?std::bit_ceil(mul_size(2,capacity)):0;
        return add_size(add_size(sizeof(MomentCache),helper_reservation),
            add_size(mul_size(capacity,sizeof(Entry)),mul_size(slots,sizeof(std::size_t))));
    }
    explicit MomentCache(std::size_t capacity=8192,std::size_t byte_budget=8*1024*1024):limit_(capacity) {
        if(reservation(capacity)>byte_budget)throw Error("polynomial chip cache workspace exceeded");
        entries_.resize(capacity);if(capacity)slots_.assign(std::bit_ceil(2*capacity),empty);
        if(working_bytes()>byte_budget)throw Error("polynomial chip actual cache workspace exceeded");
    }
    MomentCache(const MomentCache&)=delete;MomentCache& operator=(const MomentCache&)=delete;
    std::size_t working_bytes()const{return sizeof(*this)+helper_reservation+entries_.capacity()*sizeof(Entry)+slots_.capacity()*sizeof(std::size_t);}
    std::size_t capacity()const{return limit_;}
    const Counters& counters()const{return counters_;}
    // Immediate-use reference only. Do not retain a borrow across any cache
    // mutation, trim or scope exit. Caller serializes access to this cache.
    const Moments* find(const PhysicalKey& key,std::stop_token stop={}) {
        check_stop(stop);validate_key(key);increment(counters_.queries);if(!limit_)return nullptr;
        const auto at=position(key,stop),index=slots_[at];
        if(index==empty || index==tombstone)return nullptr;
        touch(index);increment(counters_.hits);return &entries_[index].moments;
    }
    const Moments& insert(const PhysicalKey& key,std::span<const float> pcm,
            std::span<const Complex> oscillator,std::stop_token stop={}) {
        check_stop(stop);validate_key(key);if(!limit_)throw Error("polynomial chip cache disabled");
        // Complete all arithmetic/cancellation before changing cache ownership.
        auto prepared=build_moments(pcm,oscillator,stop);
        auto at=position(key,stop),index=slots_[at];
        if(index!=empty && index!=tombstone){touch(index);return entries_[index].moments;}
        if(used_<limit_)index=used_++;
        else {
            index=tail_;auto& victim=entries_[index];unlink(index);victim.valid=false;
            slots_[victim.slot]=tombstone;++tombstones_;increment(counters_.evictions);
        }
        if(tombstones_>limit_/2){rehash();at=position(key);}else at=position(key);
        if(slots_[at]==tombstone)--tombstones_;
        auto& entry=entries_[index];entry.key=key;entry.moments=prepared;entry.slot=at;
        entry.previous=entry.next=empty;entry.valid=false;touch(index);entry.valid=true;slots_[at]=index;
        increment(counters_.builds);increment(counters_.physical_sample_rows,cell_samples);return entry.moments;
    }
    // Caller proves prepared moments came from exactly this key's captured34
    // rows, actual Original carrier/fence and a complete certified producer.
    // FFT input work belongs to producer counters; do not add34 physical rows
    // for each overlapping cached output. No query/hit is counted here.
    const Moments& insert_certified(const PhysicalKey& key,const Moments& prepared,std::stop_token stop={}) {
        check_stop(stop);validate_key(key);
        if(!limit_ || !valid_moments(prepared))throw Error("invalid prepared polynomial chip moments");
        auto at=position(key,stop),index=slots_[at];
        if(index!=empty && index!=tombstone){touch(index);return entries_[index].moments;}
        if(used_<limit_)index=used_++;
        else {
            index=tail_;auto& victim=entries_[index];unlink(index);victim.valid=false;
            slots_[victim.slot]=tombstone;++tombstones_;increment(counters_.evictions);
        }
        if(tombstones_>limit_/2){rehash();at=position(key);}else at=position(key);
        if(slots_[at]==tombstone)--tombstones_;
        auto& entry=entries_[index];entry.key=key;entry.moments=prepared;entry.slot=at;
        entry.previous=entry.next=empty;entry.valid=false;touch(index);entry.valid=true;slots_[at]=index;
        increment(counters_.builds);increment(counters_.certified_builds);return entry.moments;
    }
    const Moments& get_or_build(const PhysicalKey& key,std::span<const float> pcm,
            std::span<const Complex> oscillator,std::stop_token stop={}) {
        if(const auto* hit=find(key,stop))return *hit;return insert(key,pcm,oscillator,stop);
    }
    void clear(bool release=false) {
        for(auto& entry:entries_)entry.valid=false;
        std::fill(slots_.begin(),slots_.end(),empty);used_=0;head_=tail_=empty;tombstones_=0;increment(counters_.clears);
        if(release){std::vector<Entry>().swap(entries_);std::vector<std::size_t>().swap(slots_);limit_=0;}
    }
};
} // namespace datapump::modem::detail::scheduled_polynomial_chip
