#pragma once
#include "scheduled_pulse_chord_certificate.hpp"
#include "scheduled_pulse_derivative_chord_certificate.hpp"
#include <cstdlib>
#include <string_view>
#include "datapump/fhss_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include <string>
#include <utility>

namespace datapump::modem::detail::scheduled_gain_chord {
namespace cert=scheduled_acquisition_certificate;
namespace chord=scheduled_pulse_chord;
namespace derivative=scheduled_pulse_derivative_chord;
using Pair=chord::Pair;

// This is an EXPLICIT operator/root attestation, never an inferred libm
// property. Before setting it for the child process, root must qualify the
// ACTUAL linked application's fmodl path and resolved DSO's full SHA256.
// These compile/type guards only reject mismatched providers; they cannot
// qualify one. Missing/unknown tags always leave FHSS on coarse fallback.
inline bool periodic_cell_math_qualified() {
#if defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__) && __GLIBC__==2 && __GLIBC_MINOR__==41
    constexpr std::string_view inspected="x86_64-glibc-2.41-fprem-sha256-6d567d53e895273ca14a1f9dc164fc6c8d39aed2f60aa46a733c2784228915f3";
    const auto* provider=std::getenv("DATAPUMP_SCHED_PERIODIC_CELL_PROVIDER");
    return provider && std::string_view(provider)==inspected &&
        std::numeric_limits<long double>::digits==64 && sizeof(long double)==16 && cert::coordinate_environment();
#else
    return false;
#endif
}

struct GainProof {
    double magnitude=cert::infinity,error=cert::infinity,slope=cert::infinity;
    double curvature=cert::infinity,chord_error=cert::infinity;
    bool valid=false;
};
inline double unit_error(double ramp) {
    if(!(ramp>0) || !std::isfinite(ramp))return cert::infinity;
    const auto ud=std::numeric_limits<double>::epsilon()/2;
    const auto ul=static_cast<double>(std::numeric_limits<long double>::epsilon()/2);
    const auto eta=std::numeric_limits<double>::denorm_min();
    return cert::add(cert::add(cert::gamma(8,ul),cert::mul(cert::add(1,cert::gamma(8,ul)),cert::gamma(2,ud))),
        cert::mul(32*eta,cert::add(1,cert::divide(1,ramp))));
}
// All errors are SIGNED absolute error bounds, not magnitude-only envelopes.
// fhss_remainder is the separately qualified native fmodl remainder error;
// infinity explicitly disables a tight FHSS gain proof, retaining coarse caps.
inline GainProof gain_proof(const SchedHopLocation& location,const FhssPowerShape& fhss,
        double width_ticks,double coordinate_error,double fhss_coordinate_error,
        bool native_math_qualified,bool constant_one=false,double fhss_remainder=cert::infinity) {
    GainProof out;
    if(!native_math_qualified || !cert::coordinate_environment() || !location.active ||
       !std::isfinite(width_ticks) || width_ticks<0 || !std::isfinite(coordinate_error) || coordinate_error<0)return out;
    if(constant_one && !fhss.enabled)return {1,0,0,0,0,true};
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto polynomial=cert::add(cert::mul(31,cert::gamma(8,u)),128*eta);
    double es=0,ls=0,ms=0;
    if(!location.continuous) {
        if(location.flat_begin<=location.rise_begin || location.fall_end<=location.flat_end)return out;
        const auto ramp=cert::down(static_cast<double>(std::min(location.flat_begin-location.rise_begin,location.fall_end-location.flat_end)));
        ls=cert::divide(1.875,ramp);ms=cert::divide(cert::divide(6,ramp),ramp);
        es=cert::add(cert::mul(ls,coordinate_error),cert::add(cert::mul(1.875,unit_error(ramp)),polynomial));
    }
    double eh=0,lh=0,mh=0;
    if(fhss.enabled) {
        if(!fhss.smooth || !std::isfinite(fhss.support) || fhss.support<1 ||
           !std::isfinite(fhss_coordinate_error) || fhss_coordinate_error<0 ||
           !std::isfinite(fhss_remainder) || fhss_remainder<0)return out;
        const auto ramp=cert::down(static_cast<double>(fhss.support));
        lh=cert::divide(1.875,ramp);mh=cert::divide(cert::divide(6,ramp),ramp);
        eh=cert::add(cert::mul(lh,cert::add(coordinate_error,cert::add(fhss_coordinate_error,fhss_remainder))),
            cert::add(cert::mul(1.875,unit_error(ramp)),polynomial));
    }
    const auto single=cert::add(1,polynomial);
    out.magnitude=fhss.enabled?cert::add(cert::mul(cert::mul(single,single),cert::add(1,cert::gamma(1,u))),8*eta):single;
    out.error=fhss.enabled?cert::add(cert::mul(single,cert::add(es,eh)),
        cert::add(cert::mul(cert::gamma(1,u),cert::mul(single,single)),8*eta)):es;
    out.slope=cert::add(ls,lh);
    out.curvature=cert::add(cert::add(ms,mh),cert::mul(2,cert::mul(ls,lh)));
    out.chord_error=cert::add(cert::divide(cert::mul(out.curvature,cert::mul(width_ticks,width_ticks)),8),
        cert::add(cert::mul(2,out.error),cert::mul(8*std::numeric_limits<double>::epsilon(),out.magnitude)));
    out.valid=std::isfinite(out.magnitude) && std::isfinite(out.chord_error);
    return out;
}
// Prove the whole interval lies in ONE rational dwell's flat interior,
// before modulo reduction. This is a topology/branch proof, not a variable
// fmod-error claim. Qualified native finite fmod follows the same periodic cell
// away from all seams; rho includes the existing producer arithmetic reserve.
// Direct native endpoint/center gains must also be exactly1 as a defense.
inline bool fhss_flat_interior(const FhssPowerShape& shape,SchedHopCoordinate center,
        long double half_width_ticks,long double producer_radius,std::uint32_t rate,
        bool qualified_periodic_cell_math) {
    if(!shape.enabled)return true;
    if(!qualified_periodic_cell_math || !cert::coordinate_environment() || !shape.smooth || !rate ||
       !std::isfinite(half_width_ticks) || half_width_ticks<0 || !std::isfinite(producer_radius) || producer_radius<0 ||
       !std::isfinite(shape.lag_center) || !std::isfinite(shape.quiet) || !std::isfinite(shape.support) ||
       shape.quiet<0 || shape.support<1)return false;
    const auto two=2ULL*rate;
    const double fraction=static_cast<double>(center.fraction),lag=static_cast<double>(shape.lag_center);
    const auto halo=cert::up(static_cast<double>(half_width_ticks+producer_radius));
    auto x=cert::plus(cert::Interval::point(static_cast<double>(center.sample%two)),
        cert::plus(cert::Interval::disk(fraction,cert::ulp(fraction)),cert::Interval::disk(lag,cert::ulp(lag))));
    x=cert::plus(x,cert::Interval::disk(0,halo));
    const auto scale=cert::over(cert::times(x,cert::Interval::point(5)),cert::Interval::point(static_cast<double>(two)));
    if(!scale.bounded() || std::abs(scale.lo)>=0x1p51 || std::floor(scale.lo)!=std::floor(scale.hi))return false;
    const auto hop=std::floor(scale.lo);
    const auto dwell=cert::over(cert::Interval::point(static_cast<double>(two)),cert::Interval::point(5));
    const auto left=cert::minus(x,cert::times(cert::Interval::point(hop),dwell));
    const auto right=cert::minus(cert::times(cert::Interval::point(hop+1),dwell),x);
    const auto required=cert::add(cert::up(static_cast<double>(shape.quiet)),cert::up(static_cast<double>(shape.support)));
    if(!left.bounded() || !right.bounded() || !(left.lo>required && right.lo>required))return false;
    for(const auto offset:{-half_width_ticks,0.L,half_width_ticks}) {
        const auto tick=center.add_signed(offset);
        if(shape.gain(0,tick.sample%two+tick.fraction,rate)!=1)return false;
    }
    return true;
}
// The native distance has at most one long-double rounding after its exact
// uint64 difference. Two outward double predecessors cover that rounding and
// the cast under coordinate_environment (long-double precision >=64 bits).
inline bool active_interval(const SchedHopLocation& location,SchedHopCoordinate center,
                            long double half_width_ticks,double source_error) {
    if(!location.active || !cert::coordinate_environment() || !std::isfinite(half_width_ticks) ||
       half_width_ticks<0 || !std::isfinite(source_error) || source_error<0)return false;
    if(location.continuous)return true;
    const auto radius=cert::add(cert::up(static_cast<double>(half_width_ticks)),source_error);
    const auto left=cert::down(cert::down(static_cast<double>(center.distance({location.physical_begin,0}))));
    const auto right=cert::down(cert::down(static_cast<double>(SchedHopCoordinate{location.physical_end,0}.distance(center))));
    return std::isfinite(radius) && left>radius && right>radius;
}
inline double complex_difference(std::complex<double> a,std::complex<double> b) {
    const auto re=cert::minus(cert::Interval::point(a.real()),cert::Interval::point(b.real()));
    const auto im=cert::minus(cert::Interval::point(a.imag()),cert::Interval::point(b.imag()));
    const auto er=std::max(std::abs(re.lo),std::abs(re.hi)),ei=std::max(std::abs(im.lo),std::abs(im.hi));
    return cert::root(cert::add(cert::mul(er,er),cert::mul(ei,ei)));
}
struct ProductProof {Pair left{},right{};std::array<double,2> residual{cert::infinity,cert::infinity};bool valid=false;};
inline ProductProof product(const chord::Result& pulse,double gain_left,double gain_right,
                            const GainProof& gain,double amplitude) {
    ProductProof out;
    if(!pulse.valid || !gain.valid || !std::isfinite(gain_left) || !std::isfinite(gain_right) ||
       gain_left<0 || gain_right<0 || gain_left>gain.magnitude || gain_right>gain.magnitude ||
       !std::isfinite(amplitude) || amplitude<=0)return out;
    const auto dg=cert::minus(cert::Interval::point(gain_right),cert::Interval::point(gain_left));
    const auto delta_gain=std::max(std::abs(dg.lo),std::abs(dg.hi));
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    for(unsigned bit=0;bit<2;++bit) {
        const auto cross=cert::mul(.25,cert::mul(delta_gain,complex_difference(pulse.right[bit],pulse.left[bit])));
        // Target and endpoints may execute (g*limited)/A rather than g*(limited/A).
        // Two point allowances conservatively include both associations and
        // native endpoint complex multiplication. No zero pipeline is assumed.
        const auto association=cert::add(cert::mul(cert::gamma(16,u),cert::mul(gain.magnitude,pulse.maximum_norm[bit])),
            cert::mul(128*eta,cert::mul(cert::add(1,gain.magnitude),cert::add(1,cert::divide(1,amplitude)))));
        out.residual[bit]=cert::add(cert::add(cert::mul(gain.magnitude,pulse.residual[bit]),
            cert::mul(gain.chord_error,pulse.maximum_norm[bit])),cert::add(cross,cert::mul(2,association)));
        out.left[bit]=gain_left*pulse.left[bit];out.right[bit]=gain_right*pulse.right[bit];
        if(!cert::finite(out.left[bit]) || !cert::finite(out.right[bit]) || !std::isfinite(out.residual[bit]))return {};
    }
    out.valid=true;return out;
}
inline double source_coordinate_error(double displacement,double first,double last) {
    return cert::add(displacement,cert::add(cert::add(cert::ulp(first),cert::ulp(last)),
        cert::mul(16*std::numeric_limits<double>::epsilon(),cert::add(1,cert::add(std::abs(first),std::abs(last))))));
}
struct RowProof {
    ProductProof value;double gain_upper=cert::infinity,coordinate_error=cert::infinity;
    std::size_t evaluations=0;bool fhss_flat=false;
};
// Caller proves one active Sched group and strict source-window interior on
// the complete enlarged interval. Request.flat_gain=1 certifies u independently
// of its varying physical gate; this never asserts that actual gain is flat.
template<class Original>RowProof certify_row(const chord::Request& request,Original&& original,
        const SchedHopLocation& location,const FhssPowerShape& shape,SchedHopCoordinate center,
        long double half_width_ticks,long double producer_radius,long double fhss_coordinate_radius,
        std::uint32_t sample_rate,double gain_left,double gain_right,bool scheduled_flat,
        bool qualified_periodic_cell_math,std::stop_token stop={},
        const derivative::DerivativeTable* derivative_table=nullptr,
        derivative::ExecutionReport* derivative_report=nullptr) {
    RowProof out;if(request.flat_gain!=1 || !request.flat_gain_proven || !request.original_math_qualified)return out;
    out.coordinate_error=source_coordinate_error(cert::up(static_cast<double>(producer_radius)),request.first,request.last);
    if(!active_interval(location,center,half_width_ticks,out.coordinate_error))return out;
    out.fhss_flat=fhss_flat_interior(shape,center,half_width_ticks,
        static_cast<long double>(out.coordinate_error)+fhss_coordinate_radius,sample_rate,qualified_periodic_cell_math);
    auto bounded_shape=shape;if(out.fhss_flat)bounded_shape.enabled=false;
    const auto radius=cert::add(cert::up(static_cast<double>(half_width_ticks)),out.coordinate_error);
    const bool flat_interval=location.continuous ||
        (cert::down(cert::down(static_cast<double>(center.distance({location.flat_begin,0}))))>radius &&
         cert::down(cert::down(static_cast<double>(SchedHopCoordinate{location.flat_end,0}.distance(center))))>radius);
    const auto gain=gain_proof(location,bounded_shape,cert::up(static_cast<double>(2*half_width_ticks)),
        out.coordinate_error,cert::up(static_cast<double>(fhss_coordinate_radius)),request.original_math_qualified,
        scheduled_flat && flat_interval && !bounded_shape.enabled);
    if(!gain.valid)return out;out.gain_upper=gain.magnitude;
    const auto pulse=derivative_table?
        derivative::certify(request,*derivative_table,std::forward<Original>(original),stop,derivative_report):
        chord::certify(request,std::forward<Original>(original),stop);
    out.evaluations=pulse.evaluations;
    out.value=product(pulse,gain_left,gain_right,gain,request.amplitude);return out;
}
struct LatticeBound {double full_l1=cert::infinity;bool valid=false;};
inline const LatticeBound& lattice_bound() {
    static const auto bound=[] {
        LatticeBound out;if(!cert::coordinate_environment() || !chord::table_bounds().valid)return out;
        double peak=0;
        // Every argument here is an exact dyadic table knot. Between adjacent
        // knots sum |linear pulse| is convex. A finite-support boundary knot
        // retains its nonzero endpoint; deleting a term outside only reduces
        // this positive sum, including either one-sided limit.
        for(unsigned k=0;k<=256;++k) {
            const auto phase=static_cast<double>(k)/256.;double sum=0;
            for(int j=-9;j<=9;++j)sum=cert::add(sum,std::abs(pattern_pulse(phase-j-.5)));
            peak=std::max(peak,sum);
        }
        out.full_l1=peak;out.valid=std::isfinite(peak);return out;
    }();return bound;
}
// Uniform over the requested local source interval; a remote partial chip adds
// no term. This uses the ACTUAL stored finite table, not an analytic pulse or
// an empirical max. Same native original-producer contract as chord::Request.
inline double raw_norm_upper(double first,double last,std::uint64_t symbol,std::uint64_t chip,
                             double coefficient_upper,bool native_math_qualified) {
    if(!native_math_qualified || !cert::coordinate_environment() || !std::isfinite(first) || !std::isfinite(last) ||
       first<0 || first>last || last>static_cast<double>(symbol) || !chip || symbol/chip<16 ||
       symbol>0x10000000000ULL || !(coefficient_upper>0) || coefficient_upper>32 || !std::isfinite(coefficient_upper))return cert::infinity;
    const auto lattice=lattice_bound();const auto table=chord::table_bounds();if(!lattice.valid || !table.valid)return cert::infinity;
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto argument=cert::mul(9,cert::gamma(4,u));
    double mass=lattice.full_l1;
    const auto tail=symbol%chip;
    if(tail) {
        const auto center=static_cast<double>(symbol-tail)+.5*static_cast<double>(tail);
        const auto radius=cert::mul(cert::add(8,argument),static_cast<double>(chip));
        if(last>=cert::down(center-radius) && first<=cert::up(center+radius))mass=cert::add(mass,table.peak);
    }
    const auto terms=cert::mul(20,coefficient_upper);
    auto execution=cert::mul(terms,cert::add(cert::mul(table.slope,argument),cert::mul(table.peak,cert::gamma(256,u))));
    execution=cert::add(execution,cert::mul(4096*eta,cert::add(1,cert::add(coefficient_upper,cert::add(table.peak,table.slope)))));
    // No implicit continuity through +/-8: a rounded native argument may
    // admit an ideal-outside endpoint pulse. Charge every possible contributor
    // its complete cutoff jump; tightening the at-most-edge count is optional.
    execution=cert::add(execution,cert::mul(terms,table.jump));
    return cert::add(cert::mul(coefficient_upper,mass),execution);
}
inline double limited_gain_norm_upper(double raw,double amplitude,double gain_upper) {
    if(!std::isfinite(raw) || raw<0 || !std::isfinite(amplitude) || amplitude<=0 ||
       !std::isfinite(gain_upper) || gain_upper<0)return cert::infinity;
    // The producer squares both components in std::norm before limiting.
    // Reject domains where its finite-pipeline proof could overflow.
    const auto scaled=cert::mul(amplitude,raw);
    if(!std::isfinite(scaled) || scaled>std::sqrt(std::numeric_limits<double>::max())/4)return cert::infinity;
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto base=std::min(raw,cert::up(pattern_pcm_radius_limit/amplitude));
    return cert::add(cert::mul(cert::mul(base,gain_upper),cert::add(1,cert::gamma(32,u))),
        cert::mul(256*eta,cert::add(1,cert::divide(gain_upper,amplitude))));
}
inline double coarse_residual(double first,double last,std::uint64_t symbol,std::uint64_t chip,
        double amplitude,double gain_upper,double basis_norm,double fallback,bool qualified,
        double* target_upper=nullptr) {
    if(!std::isfinite(first) || !std::isfinite(last) || first>last) {
        if(target_upper)*target_upper=cert::infinity;return fallback;
    }
    // Enclose only the actual source window, including an outward cast at its
    // endpoints. Positive outside-window time never becomes an observation.
    first=std::max(0.,first);last=std::min(static_cast<double>(symbol),last);
    const auto raw=first>last?0:raw_norm_upper(first,last,symbol,chip,2,qualified);
    const auto target=limited_gain_norm_upper(raw,amplitude,gain_upper);
    if(target_upper)*target_upper=target;
    if(!std::isfinite(target) || !std::isfinite(basis_norm) || basis_norm<0)return fallback;
    return std::min(fallback,cert::add(target,basis_norm));
}
enum class Kind:unsigned { flat,ramp,fhss,window };
struct Bucket {std::uint64_t rows=0;double square=0,l1=0,gram=0,target=0,basis=0;};
struct Totals {
    std::array<Bucket,4> bucket{};
    void observe(Kind kind,double residual,double basis_norm,double target_upper=0,double contamination=0) {
        auto& b=bucket[static_cast<unsigned>(kind)];if(b.rows==std::numeric_limits<std::uint64_t>::max())throw Error("gain chord diagnostic count overflow");++b.rows;
        b.square=cert::add(b.square,cert::mul(residual,residual));b.l1=cert::add(b.l1,residual);
        b.gram=cert::add(b.gram,cert::add(cert::mul(2*basis_norm,residual),cert::mul(residual,residual)));
        if(std::isfinite(target_upper))b.target=cert::add(b.target,target_upper);else b.target=cert::infinity;
        b.basis=cert::add(b.basis,contamination);
    }
    template<class Fields>void fields(Fields& f)const {
        constexpr std::array<const char*,4> name{"flat","ramp","fhss","window"};
        for(unsigned k=0;k<4;++k){const auto& b=bucket[k];const auto prefix=std::string(name[k]);
            f.add((prefix+"_rows").c_str(),b.rows);f.add((prefix+"_residual_square").c_str(),b.square);
            f.add((prefix+"_residual_l1").c_str(),b.l1);f.add((prefix+"_residual_gram").c_str(),b.gram);
            f.add((prefix+"_target_envelope_l1").c_str(),b.target);f.add((prefix+"_basis_contamination_l1").c_str(),b.basis);}
    }
};
} // namespace datapump::modem::detail::scheduled_gain_chord
