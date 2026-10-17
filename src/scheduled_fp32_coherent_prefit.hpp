#pragma once

// Rejection-only native source proxy. No object here can be adopted as an
// authoritative receiver Fit, template cache row, carry, bit or absence result.
#include "scheduled_fp32_pulse_tile.hpp"
#include "scheduled_cold_colored_certificate.hpp"
#include <openssl/crypto.h>

namespace datapump::modem::detail::scheduled_fp32_prefit {
namespace cert=scheduled_acquisition_certificate;
namespace color=scheduled_cold_colored;
using Fit=CorrelationFit;
using Projection=CorrelationProjection;
using Pair=std::array<std::complex<double>,2>;
inline constexpr double input_limit=0x1p20,phase_limit=16;
inline constexpr std::uint64_t count_limit=std::uint64_t{1}<<31;

// Every p field is an ACTUAL original prefix difference, treated as a fixed
// signed double operand. Ordinary nonnegative budgets are inflated at flush.
// The caller must prove zero initial Fits and exact original observation/owner
// order. A resident seeded Fit is outside this contract.
struct Budget {
    double d=0,h=0,a=0,r=0;
    std::uint64_t count=0;
    bool valid=true;
    void observe(const Projection& p,double proxy_component_upper,double component_radius) {
        if(!valid)return;
        const std::array<double,6> fields{p.xc,p.xs,p.cc,p.ss,p.cs,p.energy};
        for(const auto x:fields)if(!std::isfinite(x) || std::abs(x)>input_limit){valid=false;return;}
        if(!std::isfinite(proxy_component_upper) || proxy_component_upper<0 ||
           !std::isfinite(component_radius) || component_radius<0 || count==count_limit ||
           count>=(std::numeric_limits<std::size_t>::max()-32)/12){valid=false;return;}
        d+=std::abs(p.xc)+std::abs(p.xs);
        h+=std::abs(p.cc)+std::abs(p.ss)+2*std::abs(p.cs);
        a=std::max(a,proxy_component_upper);r=std::max(r,component_radius);++count;
        valid=std::isfinite(d) && std::isfinite(h);
    }
    struct Errors {double dot=cert::infinity,gram=cert::infinity;bool valid=false;};
    Errors errors()const {
        if(!valid || count>count_limit || count>(std::numeric_limits<std::size_t>::max()-32)/12 ||
           !cert::environment<double>())return {};
        if(!count)return {0,0,true};
        const auto q=cert::add(a,r);
        if(!std::isfinite(q) || q>phase_limit)return {};
        constexpr auto u=0x1p-53,eta=std::numeric_limits<double>::denorm_min();
        // H: two positive inner additions and one running addition per row.
        // The factor2 is exact in this bounded domain. D uses fewer operations.
        const auto g=cert::gamma(3*static_cast<std::size_t>(count)+8,u);
        const auto den=cert::down(1-g);
        const auto underflow_allowance=cert::mul(8*static_cast<double>(count)+32,eta);
        const auto du=cert::divide(cert::add(d,underflow_allowance),den);
        const auto hu=cert::divide(cert::add(h,underflow_allowance),den);
        // Each field's dependency path has <=9 local rounded operations and
        // its ordered accumulation. 12n+32 conservatively covers both.
        const auto graph=cert::gamma(12*static_cast<std::size_t>(count)+32,u);
        // p<=2^20, |phase|<=16 => local absolute polynomial sum<=2^30,
        // whole sum<=2^61. At most32 local operations PER FIELD can inject
        // additive underflow, each amplified by <=2^30; relative growth is
        // <1.00001 here. This per-path allowance exceeds that bound by >2^10.
        const auto tau=cert::mul(128*static_cast<double>(count)+256,cert::mul(0x1p40,eta));
        const auto source_dot=cert::mul(r,du);
        const auto source_gram=cert::mul(cert::add(cert::mul(2*a,r),cert::mul(r,r)),hu);
        const auto round_dot=cert::mul(graph,cert::mul(cert::add(q,a),du));
        const auto round_gram=cert::mul(graph,cert::mul(cert::add(cert::mul(q,q),cert::mul(a,a)),hu));
        Errors out{cert::add(source_dot,cert::add(round_dot,cert::mul(2,tau))),
                   cert::add(source_gram,cert::add(round_gram,cert::mul(2,tau))),true};
        out.valid=std::isfinite(out.dot) && std::isfinite(out.gram) && out.dot>=0 && out.gram>=0;
        return out;
    }
    color::FitIntervals enclose(const Fit& fit)const {
        if(fit.count!=count)return {};
        const auto error=errors();if(!error.valid)return {};
        auto out=color::enclose(fit,error.dot,error.gram,0);
        // Original and proxy execute precisely the same p.energy additions in
        // the same ownership/order, so their ACTUAL energy fields are equal.
        out.energy=cert::Interval::point(fit.energy);
        return color::finite(out)?out:color::FitIntervals{};
    }
};

inline void add_fit(Fit& dst,const Fit& src) {
    dst.xc+=src.xc;dst.xs+=src.xs;dst.cc+=src.cc;dst.ss+=src.ss;
    dst.cs+=src.cs;dst.energy+=src.energy;dst.count+=src.count;
}
struct SourceResult {double radius=cert::infinity,component_upper=cert::infinity;bool valid=false;};

// Native::raw already encloses the ORIGINAL ordered FP64 q contraction. These
// checks additionally prove that every possible Original q takes the literal
// limiter's unclipped branch. No libm derivative or exact-radial shortcut.
inline SourceResult unclipped(double maximum_component,double native_radius,
                              double amplitude,double maximum_gain) {
    if(!cert::environment<double>() || !std::isfinite(maximum_component) || maximum_component<0 ||
       !std::isfinite(native_radius) || native_radius<0 || !std::isfinite(amplitude) || amplitude<=0 ||
       !std::isfinite(maximum_gain) || maximum_gain<0)return {};
    const auto b=cert::add(maximum_component,native_radius),y=cert::mul(amplitude,b);
    const auto power=cert::add(cert::mul(y,y),cert::mul(y,y));
    const auto gained=cert::mul(maximum_gain,y),divided=cert::divide(gained,amplitude);
    const auto threshold=pattern_pcm_radius_limit*pattern_pcm_radius_limit;
    if(!std::isfinite(b) || !std::isfinite(y) || !std::isfinite(power) ||
       !std::isfinite(gained) || !std::isfinite(divided) || power>threshold)return {};
    constexpr auto u=0x1p-53,eta=std::numeric_limits<double>::denorm_min();
    const auto t=cert::add(1,u);
    const auto rounded=cert::add(cert::mul(cert::gamma(3,u),cert::mul(maximum_gain,b)),
        cert::add(cert::mul(cert::divide(eta,amplitude),
            cert::add(cert::mul(maximum_gain,cert::mul(t,t)),t)),eta));
    SourceResult result{cert::add(cert::mul(maximum_gain,native_radius),cert::mul(2,rounded)),
                        cert::add(cert::mul(maximum_gain,maximum_component),rounded),true};
    result.valid=std::isfinite(result.radius) && std::isfinite(result.component_upper) &&
        cert::add(result.component_upper,result.radius)<=phase_limit;
    return result;
}

struct State {
    struct Accumulator {std::array<Fit,2> fit{};Budget budget;};
    struct Symbol {
        Accumulator whole,cell;
        std::array<color::Summary,2> chips{};
        std::uint64_t owner=color::no_owner;
        bool valid=true,finished=false;
        Symbol(){for(auto& x:chips)x.owner_set_certified=true;}
        void flush() {
            if(!cell.budget.count)return;
            for(unsigned bit=0;bit<2;++bit)chips[bit].retain(cell.budget.enclose(cell.fit[bit]));
            cell={};
        }
        void append(const Projection& p,const Pair& phases,double upper,double radius,std::uint64_t chip) {
            if(finished || !valid)return;
            if(owner!=chip){if(owner!=color::no_owner && chip<owner){valid=false;return;}flush();owner=chip;}
            for(const auto q:phases)if(!cert::finite(q) || std::abs(q.real())>upper || std::abs(q.imag())>upper){valid=false;return;}
            whole.budget.observe(p,upper,radius);cell.budget.observe(p,upper,radius);
            if(!whole.budget.valid || !cell.budget.valid){valid=false;return;}
            for(unsigned bit=0;bit<2;++bit){Fit contribution;contribution.add(p,phases[bit],1);
                add_fit(whole.fit[bit],contribution);add_fit(cell.fit[bit],contribution);}
        }
        bool finish() {if(finished)return valid;flush();finished=true;
            // Chip rank uncertainty disables colored evidence only. The whole
            // enclosure is independent, but its exact count remains required.
            for(unsigned bit=0;bit<2;++bit)valid=valid && whole.fit[bit].count==whole.budget.count;
            return valid;}
    };
    Symbol current,carry;
    void append(bool next,const Projection& p,const Pair& phases,double upper,double radius,std::uint64_t chip) {
        (next?carry:current).append(p,phases,upper,radius,chip);
    }
};

// The same actual-field interval arithmetic as score_upper. Unknown rank or
// energy returns infinity; Original log1p's qualified error is caller-owned.
inline double raw_upper(const color::FitIntervals& f,double log_error) {
    if(!color::finite(f) || !std::isfinite(log_error) || log_error<0)return cert::infinity;
    if(f.count<=2 || f.energy.hi<=1e-30)return 0;
    if(f.energy.lo<=1e-30)return cert::infinity;
    const auto explained=color::explained(f);
    if(!explained.bounded())return cert::infinity;
    const auto fraction=cert::divide(std::max(0.,explained.hi),f.energy.lo);
    if(!(fraction<1))return cert::infinity;
    return cert::mul(.5*static_cast<double>(f.count-2),
        cert::add(cert::divide(fraction,cert::down(1-fraction)),log_error));
}
struct Result {double raw=cert::infinity,upper=cert::infinity;bool ready=false;};
struct Unavailable {};
struct Frozen {
    std::array<color::FitIntervals,2> whole{};
    std::array<color::Summary,2> chips{};
    bool ready=false;
};
inline Frozen freeze(State& state) {
    Frozen out;
    if(!state.current.finish() || !state.carry.finish())return out;
    for(unsigned bit=0;bit<2;++bit){
        out.whole[bit]=state.current.whole.budget.enclose(state.current.whole.fit[bit]);
        if(!color::finite(out.whole[bit]))return {};
        out.chips[bit]=state.current.chips[bit];
    }
    out.ready=true;return out;
}
inline Result evaluate(const Frozen& frozen,double threshold,color::MathContract math,bool raw_only=false) {
    if(!frozen.ready)return {};
    Result out{0,0,true};
    for(unsigned bit=0;bit<2;++bit){const auto& whole=frozen.whole[bit];
        const auto raw=raw_upper(whole,math.log_error);auto upper=raw;
        const auto& chips=frozen.chips[bit];
        if(!raw_only && chips.valid && chips.count==whole.count){
            const auto guard=color::guarded_upper(whole,chips,threshold,math);
            if(guard.colored)upper=std::min(upper,guard.score_upper);}
        out.raw=std::max(out.raw,raw);out.upper=std::max(out.upper,upper);
    }
    out.ready=std::isfinite(out.upper) && out.upper>=0;return out;
}
inline Result finish(State& state,double threshold,color::MathContract math,bool raw_only=false) {
    return evaluate(freeze(state),threshold,math,raw_only);
}

// One heap payload, reused for each bounded source tile in one cold event.
// Four-event scheduling can reuse this payload without adding a q/H matrix.
struct Scratch {
    using Native=ScheduledFp32PulseTile<64,96>;
    using Original=Native::Original;
    typename Original::Plan plan;
    typename Native::PackedPlan packed;
    typename Native::CoefficientInput coefficient{};
    Native native;
    typename Original::Statistics statistics;
    std::array<double,64> coordinates{},gain{};
    std::array<Pair,64> q{};
    std::array<std::uint64_t,64> observed{},owner{};
    std::array<bool,64> carry{};
    std::uint64_t tile_end=0,target_chip=std::numeric_limits<std::uint64_t>::max();
    std::size_t count=0,next_row=0;
    State state;
    std::uint64_t native_rows=0,native_tiles=0,fallback_tiles=0;
    static constexpr std::size_t callee_bytes=Original::external_callee_allowance+8192;
    ~Scratch(){
        static_assert(std::is_trivially_destructible_v<typename Original::Plan> &&
            std::is_trivially_destructible_v<typename Native::PackedPlan> &&
            std::is_trivially_destructible_v<Native> && std::is_trivially_destructible_v<State>);
        OPENSSL_cleanse(this,sizeof(*this));
    }
    bool evaluate(std::size_t rows,PatternCode& code,const Config& config,std::uint64_t phase,
                  std::uint64_t first_chip,double amplitude,std::stop_token stop={}) {
        if(!rows || rows>coordinates.size() || !Native::supported() || !cert::coordinate_environment() ||
           code.transmit_trace_enabled())return false;
        for(std::size_t i=1;i<rows;++i)if(carry[i]!=carry[0])return false;
        if(!plan.build_original({coordinates.data(),rows},code.symbol_samples(),code.chip_samples(),statistics,stop) ||
           !packed.pack(plan,native.statistics,stop))return false;
        const typename Native::Lane lane{&code,&config,phase,first_chip,plan.first_local};
        if(!Native::gather(packed,lane,coefficient,native.statistics,stop) ||
           !native.prepare(packed,coefficient,typename Native::Accuracy{1e-5},stop) || !native.evaluate(stop))return false;
        // Count real native contraction even when a later proof is unavailable.
        native_rows+=rows;++native_tiles;
        double maximum=0,radius=0,gain_upper=0;
        for(std::size_t i=0;i<rows;++i){double r=0;if(!native.raw(i,q[i],&r) || !std::isfinite(r) || r<0)return false;
            radius=std::max(radius,r);
            if(!std::isfinite(gain[i]))return false;gain_upper=std::max(gain_upper,std::abs(gain[i]));
            for(const auto z:q[i]){if(!cert::finite(z))return false;maximum=std::max({maximum,std::abs(z.real()),std::abs(z.imag())});}}
        const auto bound=unclipped(maximum,radius,amplitude,gain_upper);if(!bound.valid){++fallback_tiles;return false;}
        // These are exclusively private proxy source rows; never cache/adopt.
        for(std::size_t i=0;i<rows;++i)for(auto& z:q[i])z=gain[i]*pattern_limit_pcm(amplitude*z)/amplitude;
        final_radius=bound.radius;final_upper=bound.component_upper;
        return true;
    }
    double final_radius=cert::infinity,final_upper=cert::infinity;
};
} // namespace datapump::modem::detail::scheduled_fp32_prefit
