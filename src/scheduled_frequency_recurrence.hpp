#pragma once
// Isolated proposal: short ORIGINAL carrier recurrence segments certify shared
// physical-frequency moments without constructing every target sample. Actual
// seeds and steps are exact reference operands; no libm phase accuracy assumed.
#include "scheduled_frequency_moments.hpp"

namespace datapump::modem::detail::scheduled_frequency_recurrence {
namespace fusion=scheduled_frequency_moments;
namespace tile=scheduled_acquisition_tile;
namespace cert=scheduled_acquisition_certificate;
using Complex=std::complex<double>;
using CI=fusion::ComplexInterval;
using Polynomial=std::array<CI,fusion::maximum_degree+1>;
inline constexpr std::size_t maximum_reset_segment=512;
struct Segment {
    std::size_t begin=0,end=0;
    Complex coarse_seed{},target_seed{},coarse_step{},target_step{};
    // Original capture recurrence reset identity, not the transform-family v.
    std::uint64_t original_block_first=0;std::size_t original_block_offset=0;
};
struct Contract {
    // Explicit trusted PRODUCER contract. Every segment starts at the ACTUAL
    // executed original coarse/target oscillator value at begin; subsequent
    // values use these original steps with standard rounded complex<double>
    // multiplication, no other resets/inserts/replays inside the segment.
    // Segments cover the COMPLETE physical input including all timing starts.
    // Caller preserves original phase/reference/frequency/block identity and
    // can reproduce original target samples for direct refinement. Unknown is
    // false, never infer this from frequency labels or GPSDO configuration.
    bool original_seed_step_proven=false;
};
// Acquisition tile coordinates may label a translated template family rather
// than original capture positions. Anchor modulation to an ACTUAL ORIGINAL
// coarse/target seed at local_index instead. Those seeds retain absolute carrier
// phase across physical gaps; future proofs measure every original reset seed.
// Scalar ratio/polar/coefficient rounding is part of the represented model and
// is measured by certify, not assumed exact here. No nominal-frequency shortcut.
template<class T> fusion::Coefficients anchored_coefficients(const fusion::MomentInput<T>& input,double delta_hz,
    std::uint32_t sample_rate,std::uint64_t original_phase_origin,std::size_t local_index,
    Complex coarse_seed,Complex target_seed) {
    if(local_index>=input.base.original.pcm.size() || !tile::finite(coarse_seed) || !tile::finite(target_seed) ||
       coarse_seed!=input.base.original.oscillator[local_index] || !(cert::norm(coarse_seed)>0))
        throw Error("invalid original frequency anchor");
    auto c=fusion::coefficients(input,delta_hz,sample_rate,original_phase_origin);
    using Wide=std::complex<long double>;
    const auto beta=2*std::numbers::pi_v<long double>*static_cast<long double>(delta_hz)*input.radius_samples/sample_rate;
    const auto v=(static_cast<long double>(local_index)-input.center_sample)/input.radius_samples;
    const auto ratio=Wide{target_seed.real(),target_seed.imag()}/Wide{coarse_seed.real(),coarse_seed.imag()};
    const auto center=ratio*std::polar(1.L,-beta*v);
    Wide d=center,q=center*center;
    for(unsigned k=0;k<=input.degree;++k) {
        if(k){d*=Wide{0,beta/static_cast<long double>(k)};q*=Wide{0,2*beta/static_cast<long double>(k)};}
        c.dot[k]={static_cast<double>(d.real()),static_cast<double>(d.imag())};
        c.square[k]={static_cast<double>(q.real()),static_cast<double>(q.imag())};
        if(!tile::finite(c.dot[k]) || !tile::finite(c.square[k]))throw Error("original frequency anchor overflow");
    }return c;
}

struct Work {std::uint64_t segments=0,polynomial_terms=0;};
struct CaptureOrigin {
    // Exact signed original-capture origin, derived from actual descriptor
    // counters. Kept separate from virtual Input/Value family coordinates.
    bool valid=false,negative=false;std::uint64_t magnitude=0;
};
struct Result {fusion::ModelRemainder remainder;Work work;CaptureOrigin capture_origin;};
inline CI plus(CI a,CI b){return {cert::plus(a.re,b.re),cert::plus(a.im,b.im)};}
inline CI minus(CI a,CI b){return {cert::minus(a.re,b.re),cert::minus(a.im,b.im)};}
inline CI scale(CI a,cert::Interval b){return {cert::times(a.re,b),cert::times(a.im,b)};}
inline double upper(CI a) {
    const auto re=std::max(std::abs(a.re.lo),std::abs(a.re.hi));
    const auto im=std::max(std::abs(a.im.lo),std::abs(a.im.hi));
    return cert::root(cert::add(cert::mul(re,re),cert::mul(im,im)));
}
inline cert::Interval magnitude(Complex a) {
    const auto s=cert::plus(cert::square(cert::Interval::point(a.real())),cert::square(cert::Interval::point(a.imag())));
    return {s.lo>0?std::max(0.,cert::down(std::sqrt(s.lo))):0.,cert::root(std::max(0.,s.hi))};
}
inline cert::Interval power(cert::Interval a,std::size_t n) {
    auto out=cert::Interval::point(1);
    while(n) {if(n&1)out=cert::times(out,a);n>>=1;if(n)a=cert::times(a,a);}
    return out;
}
inline double power_upper(double a,std::size_t n){return power(cert::Interval::point(a),n).hi;}
inline unsigned binomial(unsigned n,unsigned k) {
    unsigned result=1;for(unsigned i=1;i<=k;++i)result=result*(n+1-i)/i;return result;
}
inline CI evaluate(const Polynomial& p,unsigned degree,cert::Interval v) {
    CI out=p[degree];for(unsigned k=degree;k-->0;)out=plus(p[k],scale(out,v));return out;
}
inline Polynomial polynomial(const std::array<Complex,fusion::maximum_degree+1>& c,unsigned degree) {
    Polynomial out{};for(unsigned k=0;k<=degree;++k)out[k]=fusion::point(c[k]);return out;
}
inline Polynomial defect(const Polynomial& p,unsigned degree,CI a,CI b,double h,Work& work) {
    // D(v)=a*P(v+h)-b*P(v). Form the cancelled coefficients first; independently
    // bounding the two original polynomials loses their small local difference.
    Polynomial out{};std::array<cert::Interval,fusion::maximum_degree+1> hp{};
    hp[0]=cert::Interval::point(1);for(unsigned k=1;k<=degree;++k)hp[k]=cert::times(hp[k-1],cert::Interval::point(h));
    const auto difference=minus(a,b);
    for(unsigned j=0;j<=degree;++j) {
        out[j]=fusion::multiply(difference,p[j]);
        for(unsigned k=j+1;k<=degree;++k) {
            const auto shifted=scale(p[k],cert::times(cert::Interval::point(binomial(k,j)),hp[k-j]));
            out[j]=plus(out[j],fusion::multiply(a,shifted));++work.polynomial_terms;
        }
    }
    return out;
}
inline double polynomial_upper(const Polynomial& p,unsigned degree,cert::Interval v) {
    // Degree <=6, and segment width <=512/radius. Directed Horner is a valid
    // local bound; a tighter Bernstein bound is an optional separate opportunity.
    return upper(evaluate(p,degree,v));
}
inline double recurrence_error(double oscillator_max,double step_norm) {
    const auto u=std::numeric_limits<double>::epsilon()/2;
    return cert::add(cert::mul(cert::mul(cert::root(2),cert::gamma(2,u)),cert::mul(oscillator_max,step_norm)),
        cert::mul(cert::mul(cert::root(2),cert::add(3,cert::mul(2,u))),std::numeric_limits<double>::denorm_min()));
}
struct Magnitudes {double maximum=cert::infinity,error=cert::infinity;cert::Interval range{0,cert::infinity};};
inline Magnitudes oscillator_magnitudes(Complex seed,Complex step,std::size_t count) {
    const auto u=std::numeric_limits<double>::epsilon()/2;
    const auto kappa=cert::mul(cert::root(2),cert::gamma(2,u));
    const auto eta=cert::mul(cert::mul(cert::root(2),cert::add(3,cert::mul(2,u))),std::numeric_limits<double>::denorm_min());
    const auto sm=magnitude(seed),dm=magnitude(step);
    const auto q=cert::mul(cert::add(1,kappa),dm.hi);
    const auto g=std::max(1.,power_upper(q,count-1));
    Magnitudes out;out.maximum=cert::mul(g,cert::add(sm.hi,cert::mul(count-1,eta)));
    out.error=recurrence_error(out.maximum,dm.hi);
    const auto rate=power(dm,count-1);
    const auto roundoff=cert::mul(cert::mul(count-1,out.error),std::max(1.,rate.hi));
    const auto lower=cert::minus(cert::times(cert::Interval::point(std::min(1.,rate.lo)),
        cert::Interval::point(sm.lo)),cert::Interval::point(roundoff));
    out.range={std::max(0.,lower.lo),cert::add(cert::mul(std::max(1.,rate.hi),sm.hi),roundoff)};
    return out;
}
inline double represented_power_error(const std::array<Complex,fusion::maximum_degree+1>& c,
                                      unsigned degree,double vmax) {
    // v is exactly represented dyadic; stored repeated powers make k-1 rounded
    // products. Enclose their discrepancy from exact v^k (underflow included).
    const auto u=std::numeric_limits<double>::epsilon()/2;
    double out=0;for(unsigned k=2;k<=degree;++k) {
        const auto rounding=cert::add(cert::mul(cert::gamma(k-1,u),power_upper(vmax,k)),
            cert::mul(k-1,std::numeric_limits<double>::denorm_min()));
        out=cert::add(out,cert::mul(cert::norm(c[k]),rounding));
    }return out;
}
inline double relation(const Polynomial& p,const std::array<Complex,fusion::maximum_degree+1>& c,unsigned degree,
                       CI coarse_seed,CI target_seed,CI coarse_step,CI target_step,
                       double coarse_max,double coarse_error,double target_error,double v0,double v1,
                       double h,std::size_t count,Work& work) {
    const auto v=cert::Interval{std::min(v0,v1),std::max(v0,v1)};
    const auto initial=upper(minus(target_seed,fusion::multiply(coarse_seed,evaluate(p,degree,cert::Interval::point(v0)))));
    const auto d=polynomial_upper(defect(p,degree,coarse_step,target_step,h,work),degree,v);
    const auto pmax=polynomial_upper(p,degree,v);
    const auto forcing=cert::add(cert::mul(coarse_max,d),cert::add(target_error,cert::mul(coarse_error,pmax)));
    const auto g=std::max(1.,power_upper(upper(target_step),count-1));
    const auto represented=cert::mul(coarse_max,represented_power_error(c,degree,std::max(std::abs(v0),std::abs(v1))));
    return cert::add(cert::mul(g,cert::add(initial,cert::mul(count-1,forcing))),represented);
}
template<class T> Result certify(const fusion::MomentInput<T>& input,const fusion::Coefficients& c,
                                tile::Input actual_target,std::span<const Segment> segments,Contract contract,
                                std::stop_token stop={}) {
    fusion::compatible(input,c,actual_target,false);
    Result result;result.remainder=fusion::bind_certified_remainder(input,c,
        {cert::infinity,cert::infinity,cert::infinity},false);
    if(!contract.original_seed_step_proven || !cert::environment<double>() || segments.empty())return result;
    const auto n=input.base.original.pcm.size();
    if(!actual_target.oscillator.empty() && actual_target.oscillator.size()!=n)return result;
    std::size_t previous=0;std::array<double,3> error_squares{};
    const auto dot=polynomial(c.dot,input.degree),square=polynomial(c.square,input.degree);
    const auto h=1/input.radius_samples;
    for(const auto& s:segments) {
        tile::check_stop(stop);
        if(s.begin!=previous || s.end<=s.begin || s.end>n || s.end-s.begin>maximum_reset_segment ||
           !tile::finite(s.coarse_seed) || !tile::finite(s.target_seed) || !tile::finite(s.coarse_step) ||
           !tile::finite(s.target_step) || s.original_block_offset>=maximum_reset_segment ||
           s.end-s.begin>maximum_reset_segment-s.original_block_offset ||
           s.original_block_offset>std::numeric_limits<std::uint64_t>::max()-s.original_block_first ||
           s.coarse_seed!=input.base.original.oscillator[s.begin] ||
           (!actual_target.oscillator.empty() && s.target_seed!=actual_target.oscillator[s.begin]))return result;
        const auto count=s.end-s.begin;previous=s.end;++result.work.segments;
        const auto observed=input.base.count[s.end]-input.base.count[s.begin];
        if(!observed)continue;
        const auto physical=s.original_block_first+s.original_block_offset;
        auto& origin=result.capture_origin;
        if(!origin.valid) {
            origin.valid=true;origin.negative=physical<s.begin;
            origin.magnitude=origin.negative?s.begin-physical:physical-s.begin;
        }
        if(origin.negative) {
            if(s.begin<origin.magnitude || physical!=s.begin-origin.magnitude)return result;
        }else {
            if(s.begin>std::numeric_limits<std::uint64_t>::max()-origin.magnitude ||
               physical!=origin.magnitude+s.begin)return result;
        }
        const auto cm=oscillator_magnitudes(s.coarse_seed,s.coarse_step,count);
        const auto tm=oscillator_magnitudes(s.target_seed,s.target_step,count);
        const auto v0=(static_cast<double>(s.begin)-input.center_sample)/input.radius_samples;
        const auto v1=(static_cast<double>(s.end-1)-input.center_sample)/input.radius_samples;
        const auto cr=fusion::point(s.coarse_seed),tr=fusion::point(s.target_seed);
        const auto cs=fusion::point(s.coarse_step),ts=fusion::point(s.target_step);
        const auto dot_error=relation(dot,c.dot,input.degree,cr,tr,cs,ts,cm.maximum,cm.error,tm.error,
            v0,v1,h,count,result.work);
        // Original operands o^2 are exact squares here; PreparedInput's native
        // o*o preparation roundoff is covered separately by its FFT certificate.
        const auto cm2=cert::mul(cm.maximum,cm.maximum),tm2=cert::mul(tm.maximum,tm.maximum);
        const auto ce2=cert::add(cert::mul(cert::mul(2,cert::norm(s.coarse_step)),cert::mul(cm.maximum,cm.error)),cert::mul(cm.error,cm.error));
        const auto te2=cert::add(cert::mul(cert::mul(2,cert::norm(s.target_step)),cert::mul(tm.maximum,tm.error)),cert::mul(tm.error,tm.error));
        const auto square_error=relation(square,c.square,input.degree,fusion::multiply(cr,cr),fusion::multiply(tr,tr),
            fusion::multiply(cs,cs),fusion::multiply(ts,ts),cm2,ce2,te2,v0,v1,h,count,result.work);
        (void)tm2;
        const auto norm_difference=cert::minus(cert::square(tm.range),cert::square(cm.range));
        const auto norm_error=std::max(std::abs(norm_difference.lo),std::abs(norm_difference.hi));
        const auto energy=cert::minus(input.base.certified_energy[s.end],input.base.certified_energy[s.begin]);
        error_squares[0]=cert::add(error_squares[0],cert::mul(cert::mul(dot_error,dot_error),std::max(0.,energy.hi)));
        error_squares[1]=cert::add(error_squares[1],cert::mul(cert::mul(square_error,square_error),observed));
        error_squares[2]=cert::add(error_squares[2],cert::mul(cert::mul(norm_error,norm_error),observed));
    }
    if(previous!=n)return result;
    std::array<double,3> radii{};for(unsigned k=0;k<3;++k)radii[k]=cert::root(error_squares[k]);
    result.remainder=fusion::bind_certified_remainder(input,c,radii,
        std::all_of(radii.begin(),radii.end(),[](double v){return std::isfinite(v) && v>=0;}));
    return result;
}
} // namespace datapump::modem::detail::scheduled_frequency_recurrence
