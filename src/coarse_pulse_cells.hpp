#pragma once
#include "linear_grid_moments.hpp"
#include "datapump/pattern_pulse.hpp"
#include <array>
#include <algorithm>
#include <complex>
#include <cstdint>
#include <limits>
#include <stop_token>
namespace datapump::modem::detail::scheduled_coarse_pulse_cells {
namespace cert=scheduled_acquisition_certificate;namespace grid=scheduled_linear_grid;
using Pair=grid::Pair;
struct Request {
    std::uint64_t symbol_samples=0,chip_samples=0,cell=0;
    unsigned subdivisions=8;
    double amplitude=0,coordinate_radius_samples=0;
    // Same source producer/math environment contract as existing pulse-chord
    // certificate. Partial chips, nonflat gain, or changed masks go Original.
    bool original_math_qualified=false;
    // Raw finite-pulse endpoint norm only. Caller owns observation/cutoff
    // validation; this never grants a selected detector cell at0 or S.
    bool raw_endpoint_bound=false;
};
struct Result {grid::Cell cell;std::size_t coefficient_reads=0,endpoint_reads=0;double execution_radius=cert::infinity;};
inline double abs_upper(cert::Interval x){return std::max(std::abs(x.lo),std::abs(x.hi));}
inline double disk_norm(cert::Interval x,cert::Interval y){return cert::root(cert::add(cert::square(x).hi,cert::square(y).hi));}
template<unsigned Q>struct Defects {
    static_assert(Q==2||Q==4||Q==8||Q==16);
    std::array<std::array<double,20>,Q> value{};
    double peak=0,slope=0,jump=0;bool valid=false;
    Defects(){if(!cert::coordinate_environment())return;constexpr int width=256/Q;
        auto pulse=[](int tick){return std::abs(tick)>2048?0.:pattern_pulse(static_cast<double>(tick)/256.);};
        auto previous=pulse(0);peak=std::abs(previous);
        for(int i=1;i<=2048;++i){const auto now=pulse(i);peak=std::max(peak,std::abs(now));slope=std::max(slope,cert::mul(256,abs_upper(cert::minus(cert::Interval::point(now),cert::Interval::point(previous)))));previous=now;}
        jump=std::abs(previous);
        for(unsigned phase=0;phase<Q;++phase)for(int relative=-10;relative<10;++relative){
            const int start=int(phase)*width-relative*256-128;const auto a=pulse(start),b=pulse(start+width);double bound=0;
            const auto observe=[&](int tick,double actual){const auto t=static_cast<double>(tick-start)/width;
                const auto chord=cert::plus(cert::times(cert::Interval::point(1-t),cert::Interval::point(a)),cert::times(cert::Interval::point(t),cert::Interval::point(b)));
                bound=std::max(bound,abs_upper(cert::minus(cert::Interval::point(actual),chord)));};
            for(int i=0;i<=width;++i)observe(start+i,pulse(start+i));
            // Both one-sided values are required even when support is exactly
            // a cell endpoint. pattern_pulse(8) is deliberately nonzero.
            for(const int edge:{-2048,2048})if(start<=edge&&edge<=start+width){observe(edge,0);observe(edge,pulse(edge));}
            if(!std::isfinite(bound))return;value[phase][relative+10]=bound;
        }valid=true;
    }
};
template<unsigned Q>const Defects<Q>& defects(){static const Defects<Q> table;return table;}
// True ideal radial projection is enclosed around its actual native execution.
// The interval upper endpoint of Delta is used; no ordinary negative dot is
// clamped and mistaken for a proof. Universal |b-a|/4 is independent fallback.
inline double projection_chord(grid::Wide a,grid::Wide b,grid::Wide pa,grid::Wide pb,double pipeline) {
    const auto disk=[&](double x){return cert::Interval::disk(x,pipeline);};
    const auto ar=cert::Interval::point(a.real()),ai=cert::Interval::point(a.imag()),br=cert::Interval::point(b.real()),bi=cert::Interval::point(b.imag());
    const auto pr=disk(pa.real()),pi=disk(pa.imag()),qr=disk(pb.real()),qi=disk(pb.imag());
    const auto hr=cert::minus(qr,pr),hi=cert::minus(qi,pi);
    const auto rr=cert::minus(cert::minus(br,qr),cert::minus(ar,pr)),ri=cert::minus(cert::minus(bi,qi),cert::minus(ai,pi));
    const auto delta=cert::plus(cert::times(hr,rr),cert::times(hi,ri));
    const auto norm=disk_norm(cert::minus(br,ar),cert::minus(bi,ai));
    const auto firm=delta.bounded()&&delta.hi>=0?cert::mul(.5,cert::root(delta.hi)):cert::infinity;
    return std::min(cert::mul(.25,norm),firm);
}
template<unsigned Q,class Coefficients,class Endpoints>Result certify(const Request&r,Coefficients&& coefficient,Endpoints&& original,std::stop_token stop={}) {
    Result out;if(stop.stop_requested())throw Error("coarse pulse cell cancelled");
    if(!r.original_math_qualified||!cert::coordinate_environment()||r.subdivisions!=Q||!r.chip_samples||!r.symbol_samples||r.symbol_samples%r.chip_samples||
        r.symbol_samples/r.chip_samples<16||r.symbol_samples>0x10000000000ULL||r.cell>=r.symbol_samples/r.chip_samples*Q||
        !std::isfinite(r.amplitude)||r.amplitude<0x1p-128||r.amplitude>0x1p128||!std::isfinite(r.coordinate_radius_samples)||r.coordinate_radius_samples<0||r.coordinate_radius_samples>static_cast<double>(r.chip_samples)/(4*Q))return out;
    const auto& table=defects<Q>();if(!table.valid)return out;
    const auto chips=r.symbol_samples/r.chip_samples,whole=r.cell/Q;
    const auto phase=static_cast<unsigned>(r.cell%Q);
    const auto first=static_cast<double>(r.cell)*r.chip_samples/Q,last=static_cast<double>(r.cell+1)*r.chip_samples/Q;
    // Source-symbol cutoffs are owned by exact Original boundary corrections.
    if(r.raw_endpoint_bound){if(!(first>=0&&last<=r.symbol_samples))return out;}
    else if(!(first-r.coordinate_radius_samples>0&&last+r.coordinate_radius_samples<r.symbol_samples))return out;
    std::array<double,2> error{},mass{},edge_mass{};const auto begin=whole>10?whole-10:0,end=std::min(chips,whole+10);
    for(auto k=begin;k<end;++k){if(stop.stop_requested())throw Error("coarse pulse cell cancelled");
        const auto pair=coefficient(k);++out.coefficient_reads;const auto relative=static_cast<std::int64_t>(k)-static_cast<std::int64_t>(whole);
        if(relative < -10||relative>=10)return out;
        for(unsigned bit=0;bit<2;++bit){if(!cert::finite(pair[bit]))return out;const auto norm=cert::norm(pair[bit]);if(norm>32)return out;
            mass[bit]=cert::add(mass[bit],norm);if(relative==-8||relative==8)edge_mass[bit]=cert::add(edge_mass[bit],norm);error[bit]=cert::add(error[bit],cert::mul(norm,table.value[phase][relative+10]));}}
    const auto a=original(first),b=original(last);out.endpoint_reads=2;
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto argument=cert::mul(9,cert::gamma(4,u));
    for(unsigned bit=0;bit<2;++bit){if(!cert::finite(a[bit])||!cert::finite(b[bit]))return Result{};
        auto execution=cert::mul(mass[bit],cert::add(cert::mul(table.slope,argument),cert::mul(table.peak,cert::gamma(256,u))));
        execution=cert::add(execution,cert::mul(8192*eta,cert::add(1,mass[bit])));
        const auto coord=cert::mul(mass[bit],cert::mul(table.slope,cert::divide(r.coordinate_radius_samples,r.chip_samples)));
        // Existing support-jump reserve remains explicit. Coarse table defects
        // already include true one-sided jumps; this additional native reserve
        // covers argument association around support endpoints.
        const bool endpoint_edge=phase==Q/2||phase+1==Q/2;
        const auto jump=endpoint_edge?cert::mul(3,cert::mul(edge_mass[bit],table.jump)):0.;
        const auto raw_error=cert::add(error[bit],cert::add(cert::mul(2,execution),cert::add(coord,jump)));
        const auto maxraw=cert::add(std::max(cert::norm(a[bit]),cert::norm(b[bit])),raw_error);
        const auto scale=cert::add(1,cert::mul(2,maxraw));
        const auto pipeline=cert::add(cert::mul(cert::gamma(128,u),scale),cert::divide(8192*eta,r.amplitude));
        const auto pa=pattern_limit_pcm(r.amplitude*a[bit])/r.amplitude,pb=pattern_limit_pcm(r.amplitude*b[bit])/r.amplitude;
        const auto endpoint_norm=std::max(cert::norm(a[bit]),cert::norm(b[bit]));
        const auto projection=cert::mul(r.amplitude,endpoint_norm)<=pattern_pcm_radius_limit?0.:projection_chord(a[bit],b[bit],pa,pb,pipeline);
        out.cell.left[bit]=pa;out.cell.right[bit]=pb;
        out.cell.residual[bit]=cert::add(raw_error,cert::add(projection,cert::mul(4,pipeline)));
        out.execution_radius=std::isfinite(out.execution_radius)?std::max(out.execution_radius,execution):execution;
        if(!std::isfinite(out.cell.residual[bit]))return Result{};
    }
    out.cell.observed=!r.raw_endpoint_bound;out.cell.supported=true;return out;
}
template<class Coefficients,class Endpoints>Result certify(const Request&r,Coefficients&& coefficient,Endpoints&& original,std::stop_token stop={}) {
    if(r.subdivisions==8)return certify<8>(r,coefficient,original,stop);
    if(r.subdivisions==16)return certify<16>(r,coefficient,original,stop);
    if(r.subdivisions==4)return certify<4>(r,coefficient,original,stop);
    if(r.subdivisions==2)return certify<2>(r,coefficient,original,stop);
    return {};
}
} // namespace datapump::modem::detail::scheduled_coarse_pulse_cells
