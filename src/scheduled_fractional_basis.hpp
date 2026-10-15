#pragma once

// Output contraction for bounded fractional-template families. This helper does
// not establish that the ORIGINAL target template equals the mathematical mix:
// the caller supplies that separate template/carrier residual to its existing
// rejection certificate, and still refines any unproved decision in ORIGINAL.
#include "scheduled_acquisition_tile.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

namespace datapump::modem::detail::scheduled_fractional_basis {
namespace cert=scheduled_acquisition_certificate;
namespace tile=scheduled_acquisition_tile;
using Row=std::array<std::complex<double>,2>;

// The three accumulators must use identical input objects, observed masks,
// fragment masks, disjoint fragment ownership, origins and admitted starts.
// A private zero in any basis never removes an observation. Numeric equality of
// energy/count below is a defensive check, not proof of this caller invariant.
// All row/certificate storage is constant-size beyond caller-owned basis rows.
class DifferenceCertificate {
    std::array<double,2> residual_square_upper_{};
    std::uint64_t rows_=0;
    bool valid_=cert::environment<double>();
    static double difference_error(double a,double b,double executed) {
        if(a==b && executed==0)return 0;
        const auto exact=cert::minus(cert::Interval::point(a),cert::Interval::point(b));
        const auto residual=cert::minus(exact,cert::Interval::point(executed));
        return std::max(std::abs(residual.lo),std::abs(residual.hi));
    }
public:
    // Observe EVERY selected template row in EVERY fragment, once. Observing
    // additional unselected/unobserved rows only enlarges the safe bound.
    // executed must be the very qD row passed to PreparedFragment; this also
    // supports existing producers that explicitly execute the FP64 subtraction.
    void observe(const Row& a,const Row& b,const Row& executed) {
        if(rows_==std::numeric_limits<std::uint64_t>::max()) {valid_=false;return;}
        ++rows_;
        if(!valid_ || !cert::environment<double>()) {valid_=false;return;}
        for(unsigned bit=0;bit<2;++bit) {
            if(!cert::finite(a[bit]) || !cert::finite(b[bit]) || !cert::finite(executed[bit])) {
                valid_=false;return;
            }
            const auto er=difference_error(a[bit].real(),b[bit].real(),executed[bit].real());
            const auto ei=difference_error(a[bit].imag(),b[bit].imag(),executed[bit].imag());
            residual_square_upper_[bit]=cert::add(residual_square_upper_[bit],
                cert::add(cert::mul(er,er),cert::mul(ei,ei)));
            valid_=valid_ && std::isfinite(residual_square_upper_[bit]);
        }
    }
    Row subtract(const Row& a,const Row& b) {
        const Row result{a[0]-b[0],a[1]-b[1]};observe(a,b,result);return result;
    }
    void append(const DifferenceCertificate& other) {
        if(other.rows_>std::numeric_limits<std::uint64_t>::max()-rows_) {valid_=false;return;}
        rows_+=other.rows_;valid_=valid_ && other.valid_ && cert::environment<double>();
        for(unsigned bit=0;bit<2;++bit) {
            residual_square_upper_[bit]=cert::add(residual_square_upper_[bit],other.residual_square_upper_[bit]);
            valid_=valid_ && std::isfinite(residual_square_upper_[bit]);
        }
    }
    bool valid()const{return valid_ && cert::environment<double>();}
    std::uint64_t rows()const{return rows_;}
    // oscillator_norm_upper bounds the modulus of the ORIGINAL stored physical
    // oscillator on every involved sample and admitted start. Do not assume 1.
    double residual_norm_upper(unsigned bit,double oscillator_norm_upper)const {
        if(bit>1 || !valid() || !std::isfinite(oscillator_norm_upper) || oscillator_norm_upper<0)
            return cert::infinity;
        return cert::mul(oscillator_norm_upper,cert::root(residual_square_upper_[bit]));
    }
};

inline bool fields_valid(const tile::Value& value) {
    const auto& c=value.certificate;
    if(!c.valid || !std::isfinite(c.energy_lower) || !std::isfinite(c.energy_upper) ||
       c.energy_lower<0 || c.energy_lower>c.energy_upper)return false;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& f=value.fits[bit];
        if(!tile::finite(f) || f.count>0x1fffffffffffffULL || f.energy<0 ||
           !std::isfinite(c.dot_radius[bit]) || c.dot_radius[bit]<0 ||
           !std::isfinite(c.gram_radius[bit]) || c.gram_radius[bit]<0)return false;
    }
    return value.fits[0].count==value.fits[1].count &&
        value.fits[0].energy==value.fits[1].energy;
}
inline bool same_observation_fields(const tile::Value& a,const tile::Value& b) {
    return a.physical_origin==b.physical_origin && a.fits[0].count==b.fits[0].count &&
        a.fits[0].energy==b.fits[0].energy &&
        a.certificate.energy_lower==b.certificate.energy_lower &&
        a.certificate.energy_upper==b.certificate.energy_upper;
}
inline double radius_about(double center,cert::Interval bound) {
    if(!std::isfinite(center) || !bound.bounded())return cert::infinity;
    const auto delta=cert::minus(bound,cert::Interval::point(center));
    return std::max(std::abs(delta.lo),std::abs(delta.hi));
}
inline cert::Interval linear(cert::Interval a,cert::Interval b,
                             cert::Interval alpha,cert::Interval beta) {
    return cert::plus(cert::times(alpha,a),cert::times(beta,b));
}
inline double blend(double a,double b,double alpha,double beta) {
    // These centers need not reproduce an ORIGINAL reduction. Their complete
    // distance from mathematical stored-basis fields is enclosed independently.
    return std::fma(beta,b,alpha*a);
}

// t is the exact real value represented by this binary64 argument, in [0,1].
// The output certifies qmix=(1-t)qA+t*qB for the ORIGINAL STORED basis rows and
// common physical oscillator, even when native FP32 FFT accumulation produced
// a,b,d. Its error includes input certificates, coefficient/output arithmetic
// and d=FP64(qA-qB) association. Target-vs-qmix remains caller-owned.
// Failure leaves output untouched; caller must retain ORIGINAL fallback.
// Exact endpoints preserve the selected basis and do not depend on d at all.
inline bool combine(const tile::Value& a,const tile::Value& b,const tile::Value& d,
                    double t,const DifferenceCertificate& difference,
                    double oscillator_norm_upper,tile::Value& output) {
    if(!cert::environment<double>() || !std::isfinite(t) || t<0 || t>1)return false;
    if(t==0) {if(!fields_valid(a))return false;output=a;return true;}
    if(t==1) {if(!fields_valid(b))return false;output=b;return true;}
    if(!fields_valid(a) || !fields_valid(b) || !fields_valid(d) ||
       !same_observation_fields(a,b) || !same_observation_fields(a,d) || !difference.valid() ||
       difference.rows()<a.fits[0].count)return false;
    const auto beta=cert::Interval::point(t);
    // 1-t is a MATHEMATICAL complement, not a silently rounded point.
    const auto alpha=cert::minus(cert::Interval::point(1),beta);
    const auto cross=cert::times(alpha,beta);
    const double ac=1-t,bc=t,dc=ac*bc;
    tile::Value result=a;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& fa=a.fits[bit];const auto& fb=b.fits[bit];const auto& fd=d.fits[bit];
        auto& f=result.fits[bit];
        const auto eda=a.certificate.dot_radius[bit],edb=b.certificate.dot_radius[bit];
        const auto ega=a.certificate.gram_radius[bit],egb=b.certificate.gram_radius[bit];
        const auto egd=d.certificate.gram_radius[bit];
        const auto dot=[&](double av,double bv) {
            return linear(cert::Interval::disk(av,eda),cert::Interval::disk(bv,edb),alpha,beta);
        };
        const auto r=difference.residual_norm_upper(bit,oscillator_norm_upper);
        const auto trace=cert::plus(cert::Interval::disk(fd.cc,egd),cert::Interval::disk(fd.ss,egd));
        if(!std::isfinite(r) || !trace.bounded() || trace.hi<0)return false;
        // If D=(qA-qB)-delta, then ||G(D+delta)-G(D)||_2 <=
        // 2||D o||_2 ||delta o||_2 + ||delta o||_2^2. This bounds
        // every real I/Q Gram field, preserving the off-diagonal covariance.
        const auto correction=cert::mul(cross.hi,cert::add(
            cert::mul(2,cert::mul(cert::root(std::max(0.,trace.hi)),r)),cert::mul(r,r)));
        const auto gram=[&](double av,double bv,double dv) {
            return cert::plus(cert::minus(
                linear(cert::Interval::disk(av,ega),cert::Interval::disk(bv,egb),alpha,beta),
                cert::times(cross,cert::Interval::disk(dv,egd))),
                cert::Interval::disk(0,correction));
        };
        f.xc=blend(fa.xc,fb.xc,ac,bc);f.xs=blend(fa.xs,fb.xs,ac,bc);
        f.cc=std::fma(-dc,fd.cc,blend(fa.cc,fb.cc,ac,bc));
        f.ss=std::fma(-dc,fd.ss,blend(fa.ss,fb.ss,ac,bc));
        f.cs=std::fma(-dc,fd.cs,blend(fa.cs,fb.cs,ac,bc));
        // Retain the stronger complex-dot / symmetric-Gram operator disk
        // interpretation, which also bounds every scalar field consumed by
        // score_upper and cold_colored::enclose. There are no artificial
        // observations, covariance clipping, energy sums or count sums.
        const auto er=radius_about(f.xc,dot(fa.xc,fb.xc));
        const auto ei=radius_about(f.xs,dot(fa.xs,fb.xs));
        result.certificate.dot_radius[bit]=cert::root(cert::add(cert::mul(er,er),cert::mul(ei,ei)));
        const auto ecc=radius_about(f.cc,gram(fa.cc,fb.cc,fd.cc));
        const auto ess=radius_about(f.ss,gram(fa.ss,fb.ss,fd.ss));
        const auto ecs=radius_about(f.cs,gram(fa.cs,fb.cs,fd.cs));
        result.certificate.gram_radius[bit]=std::max(cert::add(ecc,ecs),cert::add(ess,ecs));
        if(!tile::finite(f) || !std::isfinite(result.certificate.dot_radius[bit]) ||
           !std::isfinite(result.certificate.gram_radius[bit]))return false;
        result.dot_roundoff[bit]=result.certificate.dot_radius[bit];
        result.gram_roundoff[bit]=result.certificate.gram_radius[bit];
        const auto determinant=f.cc*f.ss-f.cs*f.cs;
        const auto trace_center=f.cc+f.ss;
        const auto minimum=.5*(trace_center-std::hypot(f.cc-f.ss,2*f.cs));
        const auto scale=std::sqrt(std::max(0.,f.energy)*std::max(0.,trace_center));
        result.requires_refinement[bit]=f.count && (f.count<=2 || f.cc<0 || f.ss<0 ||
            !std::isfinite(determinant) || determinant<=1e-10*std::max(1.,f.cc*f.ss) ||
            !std::isfinite(minimum) || !std::isfinite(scale) ||
            minimum<=2*result.gram_roundoff[bit] ||
            result.dot_roundoff[bit]>.01*std::max(1e-30,scale));
    }
    result.certificate.valid=true;output=result;return true;
}
} // namespace datapump::modem::detail::scheduled_fractional_basis
