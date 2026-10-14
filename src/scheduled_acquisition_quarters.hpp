#pragma once

// Cold-search bounds only. The ORIGINAL scorer/guards still decide every
// plausible candidate. These helpers do not change templates, detector
// statistics, admission thresholds, calendars, trials or completion.
#include "scheduled_acquisition_certificate.hpp"
#include "pattern_drift.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace datapump::modem::detail::scheduled_acquisition_quarters {
namespace cert=scheduled_acquisition_certificate;
inline constexpr unsigned sections=4;
inline constexpr std::uint8_t not_observed=255;

// The caller supplies each ORIGINAL sample's within-source-symbol coordinate,
// computed from its physical capture time and exact calendar/rate candidate.
// Source coordinates pause during nulls; those samples have usable=0. Do not
// infer ownership from physical quarters or from fraction*4/total: partial
// symbols use the original quotient/remainder integer boundaries below.
inline std::optional<unsigned> owner(long double within,std::uint64_t total) {
    if(!total || !std::isfinite(within) || within<0 || within>=static_cast<long double>(total))return {};
    unsigned section=0;
    while(section+1<sections && within>=drift_boundary(section+1,total,sections))++section;
    return section;
}
// Fixed caller-owned storage; no sample/template copies or heap allocation.
// A failed preparation leaves the output unusable and demands original
// refinement. Non-observed coordinates are deliberately not inspected.
inline bool partition(std::span<const long double> source,
                      std::span<const std::uint8_t> usable,std::uint64_t total,
                      std::span<std::uint8_t> output) {
    if(!total || source.size()!=output.size() || (!usable.empty() && usable.size()!=source.size()))return false;
    for(std::size_t i=0;i<source.size();++i) {
        if(!usable.empty() && usable[i]>1)return false;
        if(!usable.empty() && !usable[i]){output[i]=not_observed;continue;}
        const auto section=owner(source[i],total);if(!section)return false;
        output[i]=static_cast<std::uint8_t>(*section);
    }
    return true;
}
// Adds a section ownership predicate to the EXISTING exact usable mask.
// The two complete shaped/limited row values must remain unchanged. In
// particular, finite pulse tails crossing a quarter are not cut/recomputed;
// only the observation's accumulation owner changes.
inline bool mask(std::span<const std::uint8_t> owners,unsigned section,
                 std::span<std::uint8_t> output) {
    if(section>=sections || output.size()!=owners.size())return false;
    for(std::size_t i=0;i<owners.size();++i) {
        if(owners[i]>=sections && owners[i]!=not_observed)return false;
        output[i]=owners[i]==section?1:0;
    }
    return true;
}

struct Quarter {
    std::array<CorrelationFit,2> fits{};
    cert::FitCertificate certificate;
    std::array<cert::ReferenceErrors,2> reference;
};
struct DriftReferenceErrors {
    // Proven upper error of ORIGINAL drift_evidence over the mathematical
    // function at its executed arguments. Includes effective-count arithmetic,
    // libm log/exp/log1p, the final product/subtractions and max. It is separate
    // from the fit-field certificate and coherent log1p contract. Unknown
    // defaults force original refinement; no universal libm claim is made.
    double score_error=cert::infinity;
    bool valid=false;
};

inline bool valid_certificate(const cert::FitCertificate& c,unsigned bit) {
    return bit<=1 && c.valid && std::isfinite(c.dot_radius[bit]) && c.dot_radius[bit]>=0 &&
        std::isfinite(c.gram_radius[bit]) && c.gram_radius[bit]>=0 &&
        std::isfinite(c.energy_lower) && std::isfinite(c.energy_upper) &&
        c.energy_lower>=0 && c.energy_upper>=c.energy_lower;
}
inline bool finite_fit(const CorrelationFit& f) {
    return std::isfinite(f.xc) && std::isfinite(f.xs) && std::isfinite(f.cc) &&
        std::isfinite(f.ss) && std::isfinite(f.cs) && std::isfinite(f.energy) && f.energy>=0;
}
inline bool valid_reference(const cert::ReferenceErrors& r,bool score=false) {
    return r.valid && std::isfinite(r.dot) && r.dot>=0 && std::isfinite(r.gram) && r.gram>=0 &&
        std::isfinite(r.energy) && r.energy>=0 && (!score || (std::isfinite(r.log_error) && r.log_error>=0));
}

// Additive real I/Q sufficient statistics make a fifth, whole-symbol FFT
// unnecessary after the four disjoint quarter outputs are ready. This merger
// covers its OWN FP64 four-way sums and the quarter tile errors; it does not
// replace the caller's original-whole reduction/reference certificate.
// Equal counts are checked, but exact matching observation sets remain a
// caller invariant: counts alone cannot certify source/capture ownership.
inline Quarter merge(std::span<const Quarter,sections> quarters) {
    Quarter out;out.certificate.valid=true;
    out.certificate.dot_radius={0,0};out.certificate.gram_radius={0,0};
    out.certificate.energy_lower=out.certificate.energy_upper=0;
    if(!cert::environment<double>()){out.certificate.valid=false;return out;}
    const auto round=cert::gamma(sections,std::numeric_limits<double>::epsilon()/2);
    cert::Interval energy{0,0};
    for(const auto& q:quarters) {
        if(q.fits[0].count!=q.fits[1].count || !valid_certificate(q.certificate,0) ||
           !valid_certificate(q.certificate,1) || !finite_fit(q.fits[0]) || !finite_fit(q.fits[1])) {
            out.certificate.valid=false;return out;
        }
        energy=cert::plus(energy,{q.certificate.energy_lower,q.certificate.energy_upper});
    }
    out.certificate.energy_lower=std::max(0.,energy.lo);out.certificate.energy_upper=energy.hi;
    for(unsigned bit=0;bit<2;++bit) {
        std::array<double,6> magnitude{};auto& f=out.fits[bit];
        for(const auto& q:quarters) {
            const auto& v=q.fits[bit];
            if(v.count>std::numeric_limits<std::uint64_t>::max()-f.count){out.certificate.valid=false;return out;}
            f.xc+=v.xc;f.xs+=v.xs;f.cc+=v.cc;f.ss+=v.ss;f.cs+=v.cs;f.energy+=v.energy;f.count+=v.count;
            const std::array<double,6> values{v.xc,v.xs,v.cc,v.ss,v.cs,v.energy};
            for(unsigned k=0;k<values.size();++k)magnitude[k]=cert::add(magnitude[k],std::abs(values[k]));
            out.certificate.dot_radius[bit]=cert::add(out.certificate.dot_radius[bit],q.certificate.dot_radius[bit]);
            out.certificate.gram_radius[bit]=cert::add(out.certificate.gram_radius[bit],q.certificate.gram_radius[bit]);
        }
        std::array<double,6> error{};
        for(unsigned k=0;k<error.size();++k)error[k]=cert::add(cert::mul(round,magnitude[k]),
            cert::mul(sections,std::numeric_limits<double>::denorm_min()));
        const auto dot=cert::root(cert::add(cert::mul(error[0],error[0]),cert::mul(error[1],error[1])));
        const auto gram=std::max(cert::add(error[2],error[4]),cert::add(error[3],error[4]));
        out.certificate.dot_radius[bit]=cert::add(out.certificate.dot_radius[bit],dot);
        out.certificate.gram_radius[bit]=cert::add(out.certificate.gram_radius[bit],gram);
        if(!finite_fit(f) || !valid_certificate(out.certificate,bit)){out.certificate.valid=false;return out;}
    }
    return out;
}

// Enclose the EXECUTED CorrelationFit::explained(), including its original
// count/energy/determinant gates and clamp. The supplied radii must cover the
// SAME original template/sample set and original accumulated FP64 fields.
// An uncertain gate sets the lower bound to zero; a singular determinant uses
// the original clamp-to-energy upper instead of inventing a rank repair.
inline cert::Interval explained(const CorrelationFit& fit,const cert::FitCertificate& c,
                                unsigned bit,const cert::ReferenceErrors& original) {
    if(!valid_certificate(c,bit) || !valid_reference(original) || !finite_fit(fit) ||
       !cert::environment<double>() || fit.count>0x1fffffffffffffULL)
        return {0,cert::infinity};
    if(fit.count<=2)return {0,0};
    const auto ed=cert::add(c.dot_radius[bit],original.dot),eg=cert::add(c.gram_radius[bit],original.gram);
    if(!std::isfinite(ed) || ed<0 || !std::isfinite(eg) || eg<0)return {0,cert::infinity};
    const cert::Interval energy{std::max(0.,cert::down(c.energy_lower-original.energy)),
                               std::max(0.,cert::up(c.energy_upper+original.energy))};
    if(!energy.bounded())return {0,cert::infinity};
    if(energy.hi<=1e-30)return {0,0};
    const auto xc=cert::Interval::disk(fit.xc,ed),xs=cert::Interval::disk(fit.xs,ed);
    const auto cc=cert::Interval::disk(fit.cc,eg),ss=cert::Interval::disk(fit.ss,eg),cs=cert::Interval::disk(fit.cs,eg);
    if(!xc.bounded() || !xs.bounded() || !cc.bounded() || !ss.bounded() || !cs.bounded())return {0,cert::infinity};
    const auto product=cert::times(cc,ss);
    const auto determinant=cert::minus(product,cert::times(cs,cs));
    if(!determinant.bounded())return {0,energy.hi};
    const auto gate=cert::times(cert::Interval::point(1e-12),
                               {std::max(1.,product.lo),std::max(1.,product.hi)});
    if(determinant.hi<=gate.lo)return {0,0};
    if(determinant.lo<=0)return {0,energy.hi};
    const auto numerator=cert::minus(cert::plus(cert::times(cert::times(ss,xc),xc),
                                               cert::times(cert::times(cc,xs),xs)),
        cert::times(cert::times(cert::times(cert::Interval::point(2),cs),xc),xs));
    const auto projected=cert::over(numerator,determinant);
    if(!projected.bounded())return {0,energy.hi};
    const auto upper=std::min(energy.hi,std::max(0.,projected.hi));
    const bool gate_always=energy.lo>1e-30 && determinant.lo>gate.hi;
    const auto lower=gate_always?std::max(0.,std::min(energy.lo,projected.lo)):0.;
    return {lower,std::max(lower,upper)};
}

// Sum in original chronological quarter order; remove the ORIGINAL strongest
// quarter using max of interval lower bounds. Bounds include the sequential
// explained_sum additions and final subtract rather than treating them exact.
inline cert::Interval retained(std::span<const cert::Interval,sections> quarters) {
    cert::Interval sum{0,0};double largest_lower=0,largest_upper=0;
    for(const auto q:quarters) {
        if(!q.bounded() || q.lo<0)return {0,cert::infinity};
        sum=cert::plus(sum,q);largest_lower=std::max(largest_lower,q.lo);largest_upper=std::max(largest_upper,q.hi);
    }
    const auto value=cert::minus(sum,{largest_lower,largest_upper});
    return {std::max(0.,value.lo),std::max(0.,value.hi)};
}

inline double drift_upper(const CorrelationFit& whole,const cert::FitCertificate& whole_cert,unsigned bit,
                          const cert::ReferenceErrors& whole_reference,
                          std::span<const Quarter,sections> quarters,const DriftReferenceErrors& drift_reference) {
    if(!valid_certificate(whole_cert,bit) || !valid_reference(whole_reference) || !finite_fit(whole) ||
       !drift_reference.valid || !cert::environment<double>() || whole.count>0x1fffffffffffffULL ||
       !std::isfinite(drift_reference.score_error) || drift_reference.score_error<0)return cert::infinity;
    std::uint64_t count=0;std::array<cert::Interval,sections> evidence;
    for(unsigned j=0;j<sections;++j) {
        if(quarters[j].fits[bit].count>std::numeric_limits<std::uint64_t>::max()-count)return cert::infinity;
        count+=quarters[j].fits[bit].count;
        evidence[j]=explained(quarters[j].fits[bit],quarters[j].certificate,bit,quarters[j].reference[bit]);
    }
    // Nulls/zero gain/adjacent-symbol carry must not become noise degrees.
    // A changed set or incomplete ownership proof refuses pruning.
    if(count!=whole.count)return cert::infinity;
    if(whole.count<=2*sections)return cert::add(0,drift_reference.score_error);
    const auto r=retained(evidence);
    const auto energy_lower=cert::down(whole_cert.energy_lower-whole_reference.energy);
    if(!(energy_lower>1e-30) || !r.bounded())return cert::infinity;
    const auto q=cert::divide(r.hi,energy_lower);
    if(!(q<1))return cert::infinity;
    // Effective fallback count min(N,4*N/chip) never exceeds whole N. The
    // aligned full-rank scheduled branch uses N. Both are bounded here.
    // Beta polynomial >=1, so its subtraction only lowers mathematical score.
    // -log(1-q) <= q/(1-q), without calling libm in this screen.
    const auto residual=.5*static_cast<double>(whole.count)-sections;
    const auto upper=cert::mul(residual,cert::divide(q,cert::down(1-q)));
    return cert::add(upper,drift_reference.score_error);
}

inline double combined_upper(const CorrelationFit& whole,const cert::FitCertificate& c,unsigned bit,
                             const cert::ReferenceErrors& original,std::span<const Quarter,sections> quarters,
                             const DriftReferenceErrors& drift_reference,bool differential_enabled=false) {
    if(differential_enabled || !valid_certificate(c,bit) || !valid_reference(original,true) ||
       !finite_fit(whole))return cert::infinity; // Unsupported/invalid contracts remain original-only.
    const auto coherent=cert::score_upper(whole,c,bit,original);
    const auto drift=drift_upper(whole,c,bit,original,quarters,drift_reference);
    // combine_drift_evidence subtracts ln2; omit that decrease conservatively.
    // Original colored-chip and outer-presence guards only lower/veto evidence.
    return std::max(coherent,drift);
}
} // namespace datapump::modem::detail::scheduled_acquisition_quarters
