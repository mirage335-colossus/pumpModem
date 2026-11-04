#pragma once

// ARTIFACT PROPOSAL: original PatternCode::shaped_values endpoint chords.
// This is a numerical residual certificate, not a detector or a mask proof.
#include "scheduled_acquisition_certificate.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::modem::detail::scheduled_pulse_chord {
namespace cert=scheduled_acquisition_certificate;
using Pair=std::array<std::complex<double>,2>;
enum class Failure { none, contract, workspace, knot_cap, coordinate, producer, clipping, arithmetic };
struct Request {
    // EXACT stored FP64 source arguments. The eventual interpolation weight
    // must enclose (target-first)/(last-first); other weight/coordinate errors
    // belong to the caller, not this certificate.
    double first=0,last=0,amplitude=0,flat_gain=1,coefficient_norm_upper=0;
    std::uint64_t symbol_samples=0,chip_samples=0;
    std::size_t maximum_points=2048,workspace=0;
    // Explicit producer contract: original enabled finite-table pulse sum,
    // fixed key/epoch/symbol/bit identities, EXACT Request S/C geometry,
    // <=20 contributing coefficients,
    // coefficient_norm_upper valid for BOTH actual stored private rows
    // including every applied per-chip gain/rotation, and
    // qualified native FP64 std::lerp/sqrt/product/sum execution described in
    // PROOF.md (library/instruction inspection, not merely stored types).
    bool original_math_qualified=false,flat_gain_proven=false;
};
struct Result {
    Pair left{},right{}; // ACTUAL gain*limit(amplitude*q)/amplitude endpoints
    std::array<double,2> residual{cert::infinity,cert::infinity};
    std::array<double,2> maximum_norm{cert::infinity,cert::infinity};
    double execution_radius=cert::infinity,jump_radius=cert::infinity;
    std::size_t evaluations=0,points=0,support_edge_terms=0,peak_temporary_bytes=0;
    Failure failure=Failure::contract;
    bool valid=false;
};
inline void check_stop(std::stop_token stop) {
    if(stop.stop_requested())throw Error("pulse chord certificate cancelled");
}
inline std::size_t reservation(std::size_t points) {
    if(points<2 || points>4096)throw Error("pulse chord point cap unsupported");
    return 8192+2*points*sizeof(double);
}
struct TableBounds {double peak=0,slope=0,jump=0;bool valid=true;};
inline const TableBounds& table_bounds() {
    // These are ACTUAL stored table entries: dyadic i/256 lands exactly on
    // each knot. No accuracy of the table's original transcendental setup is
    // assumed. std::lerp(a,b,0) returns a exactly in the qualified producer.
    static const auto bounds=[] {TableBounds out;auto previous=pattern_pulse(0.);
    if(!std::isfinite(previous)){out.valid=false;return out;}
    out.peak=std::abs(previous);
    for(unsigned i=1;i<=2048;++i) {
        const auto value=pattern_pulse(static_cast<double>(i)/256.);
        if(!std::isfinite(value)){out.valid=false;return out;}
        out.peak=std::max(out.peak,std::abs(value));
        const auto difference=cert::minus(cert::Interval::point(value),cert::Interval::point(previous));
        out.slope=std::max(out.slope,cert::mul(256,std::max(std::abs(difference.lo),std::abs(difference.hi))));
        previous=value;
    }
    out.jump=std::abs(previous);return out;}();return bounds;
}
inline double chord_distance(Pair left,Pair right,Pair actual,unsigned bit,double first,double last,double point) {
    if(point==first || point==last)return 0;
    const auto t=cert::over(cert::minus(cert::Interval::point(point),cert::Interval::point(first)),
        cert::minus(cert::Interval::point(last),cert::Interval::point(first)));
    if(!t.bounded())return cert::infinity;
    const auto one_minus=cert::minus(cert::Interval::point(1),t);
    const auto coordinate=[&](bool imaginary) {
        const auto a=imaginary?left[bit].imag():left[bit].real();
        const auto b=imaginary?right[bit].imag():right[bit].real();
        const auto q=imaginary?actual[bit].imag():actual[bit].real();
        return cert::minus(cert::Interval::point(q),cert::plus(cert::times(one_minus,cert::Interval::point(a)),
            cert::times(t,cert::Interval::point(b))));
    };
    const auto re=coordinate(false),im=coordinate(true);
    if(!re.bounded() || !im.bounded())return cert::infinity;
    const auto a=std::max(std::abs(re.lo),std::abs(re.hi)),b=std::max(std::abs(im.lo),std::abs(im.hi));
    return cert::root(cert::add(cert::mul(a,a),cert::mul(b,b)));
}
// Callback receives one ORIGINAL double source argument and stop token, and
// returns BOTH un-limited original shaped_values rows in their original order.
template<class Original> Result certify(const Request& r,Original&& original,std::stop_token stop={}) {
    Result out;check_stop(stop);
    if(!r.original_math_qualified || !r.flat_gain_proven || !cert::coordinate_environment() ||
       !std::isfinite(r.first) || !std::isfinite(r.last) || !(r.first<r.last) ||
       !std::isfinite(r.amplitude) || !(r.amplitude>0) || !std::isfinite(r.flat_gain) ||
       r.flat_gain<0 || r.flat_gain>1 || !std::isfinite(r.coefficient_norm_upper) ||
       !(r.coefficient_norm_upper>0) || r.coefficient_norm_upper>32 ||
       !r.chip_samples || r.symbol_samples/r.chip_samples<16 || r.symbol_samples>0x10000000000ULL ||
       r.first<0 || r.last>=static_cast<double>(r.symbol_samples) ||
       static_cast<long double>(r.last)-r.first>2.L*r.chip_samples ||
       r.maximum_points<2 || r.maximum_points>4096)return out;
    out.failure=Failure::workspace;
    if(reservation(r.maximum_points)>r.workspace)return out;
    std::vector<double> points;points.reserve(r.maximum_points);
    out.peak_temporary_bytes=8192+points.capacity()*sizeof(double);
    if(out.peak_temporary_bytes>r.workspace)return out;
    points.push_back(r.first);points.push_back(r.last);
    out.failure=Failure::coordinate;
    const auto add_grid=[&](std::int64_t offset) {
        const auto c=static_cast<std::int64_t>(r.chip_samples);
        const auto lo=(static_cast<long double>(r.first)*256-offset)/c;
        const auto hi=(static_cast<long double>(r.last)*256-offset)/c;
        if(!std::isfinite(lo) || !std::isfinite(hi) || std::abs(lo)>=0x1p51L || std::abs(hi)>=0x1p51L)return false;
        // Two extra grid cells cover the bounded extended-precision quotient
        // rounding. Exact generated knots are then compared with the endpoints.
        const auto begin=static_cast<std::int64_t>(std::floor(lo))-2;
        const auto end=static_cast<std::int64_t>(std::ceil(hi))+2;
        if(end<begin || end-begin>4100)return false;
        for(auto k=begin;k<=end;++k) {
            if((static_cast<std::uint64_t>(k)&127U)==0)check_stop(stop);
            if(k>std::numeric_limits<std::int64_t>::max()/c || k<std::numeric_limits<std::int64_t>::min()/c)return false;
            const auto product=k*c;
            if((product>0 && offset>std::numeric_limits<std::int64_t>::max()-product) ||
               (product<0 && offset<std::numeric_limits<std::int64_t>::min()-product))return false;
            const auto numerator=offset+product;
            if(numerator<=-0x10000000000000LL || numerator>=0x10000000000000LL)return false;
            const auto point=static_cast<double>(numerator)/256.; // exact dyadic
            if(point<=r.first || point>=r.last)continue;
            if(points.size()==r.maximum_points){out.failure=Failure::knot_cap;return false;}
            points.push_back(point);
        }
        return true;
    };
    if(!add_grid(0))return out;
    const auto tail=r.symbol_samples%r.chip_samples;
    if(tail) {
        const auto offset=static_cast<std::int64_t>(256*(r.symbol_samples-tail)+128*tail);
        if(!add_grid(offset))return out;
    }
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    out.points=points.size();check_stop(stop);
    const auto table=table_bounds();out.failure=Failure::arithmetic;
    if(!table.valid)return out;
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto argument=cert::mul(9,cert::gamma(4,u));
    const auto terms=cert::mul(20,r.coefficient_norm_upper);
    // Table interpolation, partial-chip constant multiplication, two-real
    // complex products and original ascending sums: qualified bound, not a
    // claim that executed FP64 shaped_values is exactly affine.
    auto execution=cert::mul(terms,cert::add(cert::mul(table.slope,argument),cert::mul(table.peak,cert::gamma(256,u))));
    execution=cert::add(execution,cert::mul(4096*eta,cert::add(1,cert::add(r.coefficient_norm_upper,cert::add(table.peak,table.slope)))));
    const auto pad=cert::mul(argument,static_cast<double>(r.chip_samples));
    const auto first=cert::down(r.first-pad),last=cert::up(r.last+pad);
    const auto edge=[&](double point){if(point>=first && point<=last)++out.support_edge_terms;};
    const auto full=r.symbol_samples/r.chip_samples;
    const auto begin=std::max<std::int64_t>(0,static_cast<std::int64_t>(std::floor(static_cast<long double>(r.first)/r.chip_samples))-10);
    const auto end=std::min<std::uint64_t>(full,static_cast<std::uint64_t>(std::ceil(static_cast<long double>(r.last)/r.chip_samples))+11);
    for(auto j=static_cast<std::uint64_t>(begin);j<end;++j) {
        const auto center=static_cast<double>(j*r.chip_samples)+.5*static_cast<double>(r.chip_samples);
        edge(center-8.*r.chip_samples);edge(center+8.*r.chip_samples);
    }
    if(tail) {const auto center=static_cast<double>(r.symbol_samples-tail)+.5*static_cast<double>(tail);
        edge(center-8.*r.chip_samples);edge(center+8.*r.chip_samples);}
    const auto jump=cert::mul(cert::mul(static_cast<double>(out.support_edge_terms),r.coefficient_norm_upper),table.jump);
    out.execution_radius=execution;out.jump_radius=jump;
    const auto inflate=cert::add(cert::mul(2,execution),cert::mul(3,jump));
    Pair a{},b{};std::array<double,2> norm{},residual{};
    const auto evaluate=[&](double point,Pair& q) {
        check_stop(stop);q=original(point,stop);++out.evaluations;
        return cert::finite(q[0]) && cert::finite(q[1]) && cert::coordinate_environment();
    };
    out.failure=Failure::producer;
    if(!evaluate(r.first,a) || !evaluate(r.last,b))return out;
    for(unsigned bit=0;bit<2;++bit)norm[bit]=std::max(cert::norm(a[bit]),cert::norm(b[bit]));
    for(const auto point:points) {
        if(point==r.first || point==r.last)continue;
        Pair q;if(!evaluate(point,q))return out;
        for(unsigned bit=0;bit<2;++bit){norm[bit]=std::max(norm[bit],cert::norm(q[bit]));
            residual[bit]=std::max(residual[bit],chord_distance(a,b,q,bit,r.first,r.last,point));}
    }
    out.failure=Failure::clipping;
    std::array<double,2> maximum_raw{};
    for(unsigned bit=0;bit<2;++bit) {
        maximum_raw[bit]=cert::add(norm[bit],inflate);
        auto amplitude_norm=cert::mul(r.amplitude,maximum_raw[bit]);
        amplitude_norm=cert::add(amplitude_norm,cert::add(cert::mul(cert::gamma(4,u),amplitude_norm),8*eta));
        const auto power=cert::add(cert::mul(cert::add(1,cert::gamma(4,u)),cert::mul(amplitude_norm,amplitude_norm)),8*eta);
        if(!std::isfinite(power) || !(power<pattern_pcm_radius_limit*pattern_pcm_radius_limit))return out;
    }
    out.failure=Failure::arithmetic;
    for(unsigned bit=0;bit<2;++bit) {
        const auto scale=cert::mul(r.flat_gain,maximum_raw[bit]);
        const auto tiny=cert::add(cert::divide(cert::mul(128*eta,cert::add(1,r.flat_gain)),r.amplitude),128*eta);
        const auto pipeline=cert::add(cert::mul(cert::gamma(16,u),scale),tiny);
        out.left[bit]=r.flat_gain*pattern_limit_pcm(r.amplitude*a[bit])/r.amplitude;
        out.right[bit]=r.flat_gain*pattern_limit_pcm(r.amplitude*b[bit])/r.amplitude;
        out.residual[bit]=cert::add(cert::mul(r.flat_gain,cert::add(residual[bit],inflate)),cert::mul(2,pipeline));
        out.maximum_norm[bit]=cert::add(scale,pipeline);
        if(!cert::finite(out.left[bit]) || !cert::finite(out.right[bit]) || !std::isfinite(out.residual[bit]) ||
           !std::isfinite(out.maximum_norm[bit]))return out;
    }
    check_stop(stop);if(!cert::coordinate_environment())return out;
    out.failure=Failure::none;out.valid=true;return out;
}
} // namespace datapump::modem::detail::scheduled_pulse_chord
