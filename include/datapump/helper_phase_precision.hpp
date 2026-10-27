#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <complex>
#include <limits>
#include <numbers>
#include <span>

namespace datapump::helper::phase_detail {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t capacity=240,context_size=10;
struct Anchor {
    double seconds=0,phase=0,variance=0;
    std::uint64_t first=0,end=0;
    double joint_hz=0,thermal_phase_variance=0,process_phase_variance=0;
    double thermal_frequency_variance=0,process_frequency_variance=0;
};
struct Estimate {
    bool valid=false;
    double hz=0,slope=0,uncertainty=0,slope_uncertainty=0,chi=0,support=0;
    std::size_t count=0;
    std::uint64_t first=0,end=0;
};
struct Projection {
    std::uint64_t first=0,end=0,generation=0,origin=0;
    std::uint32_t rate=0;
    // The phase is the actual 20 ms projection phase plus accumulated mixer
    // phase, relative to a fixed initial frequency. Its integer part is free.
    double phase_cycles=0,phase_variance=0;
    double fll_hz=0,fll_uncertainty=0,fll_slope=0,reference_hz=0;
    bool level_ok=false,accepted=false,qualified=false;
    std::complex<double> value{};
    double complex_noise_variance=0;
};
struct Fit {
    bool valid=false;
    double hz=0,slope=0,frequency_sigma=0,slope_sigma=0,chi=0,response=0;
};

// GLS on phase differences removes the unknown constant carrier phase. The
// saved thermal endpoint occurs with opposite signs in neighboring edges;
// only those actual identical endpoint variances are shared. Brownian point
// increments are independent. Window-averaging allowances are already saved
// at each endpoint. Work and storage are O(N), with a fixed numeric bound.
inline Fit fit(std::span<const Anchor> anchors,double now,double sigma)noexcept {
    Fit out;if(anchors.size()<3 || anchors.size()>capacity || !(sigma>=0))return out;
    const auto span=anchors.back().seconds-anchors.front().seconds;
    if(!(span>0) || now<anchors.back().seconds)return out;
    const auto n=anchors.size()-1;
    std::array<long double,capacity> diagonal{},sub{},x{},z{},y{},curve{};
    for(std::size_t i=0;i<n;++i) {
        const auto& a=anchors[i];const auto& b=anchors[i+1];const auto dt=b.seconds-a.seconds;
        if(!(dt>0) || !(a.variance>0) || !(b.variance>0) || !std::isfinite(a.phase) || !std::isfinite(b.phase))return out;
        diagonal[i]=a.variance+b.variance+2*sigma*sigma*dt/(tau*tau);
        if(i)sub[i]=-a.variance;
        const auto ta=a.seconds-now,tb=b.seconds-now;
        x[i]=dt/span;z[i]=.5*(tb*tb-ta*ta)/(span*span);y[i]=b.phase-a.phase;
        curve[i]=(tb*tb*tb-ta*ta*ta)/6;
    }
    for(std::size_t i=0;i<n;++i) {
        if(i) {
            const auto multiplier=sub[i]/diagonal[i-1];diagonal[i]-=multiplier*sub[i];
            x[i]-=multiplier*x[i-1];z[i]-=multiplier*z[i-1];
            y[i]-=multiplier*y[i-1];curve[i]-=multiplier*curve[i-1];
        }
        if(!(diagonal[i]>0) || !std::isfinite(diagonal[i]))return out;
    }
    long double xx=0,xz=0,zz=0,xy=0,zy=0,xc=0,zc=0;
    for(std::size_t i=0;i<n;++i) {
        xx+=x[i]*x[i]/diagonal[i];xz+=x[i]*z[i]/diagonal[i];zz+=z[i]*z[i]/diagonal[i];
        xy+=x[i]*y[i]/diagonal[i];zy+=z[i]*y[i]/diagonal[i];
        xc+=x[i]*curve[i]/diagonal[i];zc+=z[i]*curve[i]/diagonal[i];
    }
    const auto determinant=xx*zz-xz*xz;
    if(!(determinant>1e-12L*xx*zz))return out;
    const auto frequency=(zz*xy-xz*zy)/determinant,slope=(xx*zy-xz*xy)/determinant;
    double chi=0;
    for(std::size_t i=0;i<n;++i) {
        const auto e=y[i]-frequency*x[i]-slope*z[i];chi+=static_cast<double>(e*e/diagonal[i]);
    }
    const auto scale=std::max(1.,chi/std::max<std::size_t>(1,n-2));
    out.hz=static_cast<double>(frequency)/span;out.slope=static_cast<double>(slope)/(span*span);
    out.frequency_sigma=std::sqrt(static_cast<double>(zz/determinant)*scale)/span;
    out.slope_sigma=std::sqrt(static_cast<double>(xx/determinant)*scale)/(span*span);
    out.response=static_cast<double>((zz*xc-xz*zc)/determinant)/span;
    out.chi=chi/std::max<std::size_t>(1,n-2);
    out.valid=std::isfinite(out.hz)&&std::isfinite(out.frequency_sigma)&&std::isfinite(out.response);
    return out;
}

// This primitive is shared by runtime and a conditional Planner forecast.
// Calling it with expected anchors predicts precision; it never qualifies a
// received signal. Numeric history span is not uninterrupted coherent tone.
inline Estimate estimate(std::span<const Anchor> anchors,double now,double sigma,double reference_hz)noexcept {
    Estimate out;if(anchors.size()<5)return out;
    const auto support=anchors.back().seconds-anchors.front().seconds;
    if(support<.7 || now-anchors.back().seconds>.5)return out;
    const auto full=fit(anchors,now,sigma);if(!full.valid)return out;
    const auto boundary=anchors.back().seconds-.5*support;
    std::size_t recent_first=0;while(recent_first<anchors.size() && anchors[recent_first].seconds<boundary)++recent_first;
    const auto recent=fit(anchors.subspan(recent_first),now,sigma);if(!recent.valid)return out;
    const auto contrast=recent.response-full.response;
    if(std::abs(contrast)<=1e-10*(std::abs(full.response)+std::abs(recent.response)))return out;
    // The fits share samples. The sum of their standard deviations is a
    // conservative contrast bound for every possible covariance. A measured
    // full/recent discrepancy retains a smooth-evolution curvature allowance.
    const auto allowance=std::abs(full.response/contrast)*(std::abs(recent.hz-full.hz)+
        3*(full.frequency_sigma+recent.frequency_sigma));
    out.hz=reference_hz+full.hz;out.slope=full.slope;
    // Legacy engineering allowance, not a derived floating-point bound or
    // a physical precision floor. Abrupt model changes remain unqualified.
    out.uncertainty=3*full.frequency_sigma+allowance+.005;
    out.slope_uncertainty=3*full.slope_sigma;out.chi=full.chi;out.support=support;out.count=anchors.size();
    out.first=anchors.front().first;out.end=anchors.back().end;
    out.valid=std::isfinite(out.uncertainty)&&std::isfinite(out.hz);return out;
}


struct JointCovariance {
    bool valid=false;
    double center=0,phase_variance=0,frequency_variance=0;
    double thermal_phase_variance=0,process_phase_variance=0;
    double thermal_frequency_variance=0,process_frequency_variance=0;
};
// Independent disjoint Hann projections: complex_noise_variance is E|n|^2.
// The unknown complex amplitude and frequency are fitted together. The phase
// date is the inverse-noise weighted center, where the Fisher cross term is
// zero; keep the full inverse formula so the nuisance is never omitted.
// A conservative Brownian endpoint allowance covers the ENTIRE raw context.
inline JointCovariance joint_covariance(std::span<const double> times,
    std::span<const double> noise,double amplitude_square,double sigma,double duration)noexcept {
    JointCovariance out;if(times.size()!=noise.size() || times.size()<3 || !(amplitude_square>0) || !(duration>0))return out;
    double sw=0,st=0;for(std::size_t i=0;i<times.size();++i){if(!(noise[i]>0))return out;sw+=1/noise[i];st+=times[i]/noise[i];}
    out.center=st/sw;double s1=0,s2=0;
    for(std::size_t i=0;i<times.size();++i){const auto t=times[i]-out.center;s1+=t/noise[i];s2+=t*t/noise[i];}
    const auto determinant=sw*s2-s1*s1;if(!(determinant>0))return out;
    out.thermal_phase_variance=s2/(2*amplitude_square*determinant*tau*tau);
    out.thermal_frequency_variance=sw/(2*amplitude_square*determinant*tau*tau);
    out.process_phase_variance=2*sigma*sigma*duration/(tau*tau);
    // These phase-to-frequency coefficients sum to zero, removing the
    // arbitrary phase preceding this context. Every nonnegative Hann
    // projection averages Brownian phase inside the full support duration.
    // Minkowski bounds their weighted combination without assuming any
    // process covariance or uniform observation weights.
    double absolute_frequency_weight=0;
    for(std::size_t i=0;i<times.size();++i){
        const auto t=times[i]-out.center;
        absolute_frequency_weight+=std::abs((sw*t-s1)/(noise[i]*determinant));
    }
    out.process_frequency_variance=sigma*sigma*duration*absolute_frequency_weight*absolute_frequency_weight/(tau*tau);
    out.phase_variance=out.thermal_phase_variance+out.process_phase_variance;
    out.frequency_variance=out.thermal_frequency_variance+out.process_frequency_variance;
    out.valid=std::isfinite(out.phase_variance)&&std::isfinite(out.frequency_variance);return out;
}
struct JointContext {
    bool valid=false;
    double phase=0,seconds=0,variance=0,hz=0,frequency_sigma=0,chi=0,snr=0;
    double thermal_phase_variance=0,process_phase_variance=0;
    double thermal_frequency_variance=0,process_frequency_variance=0;
};
inline JointContext joint_context(std::span<const Projection> rows,double sigma)noexcept {
    JointContext out;if(rows.size()!=context_size)return out;
    const auto& p=rows.back();if(!p.rate || !p.qualified || p.first<p.origin)return out;
    std::array<double,context_size> times{},noise{};std::array<std::complex<double>,context_size> values{};
    const auto duration=static_cast<double>(rows.back().end-rows.front().first)/p.rate;
    double sw=0,st=0,energy=0;
    for(std::size_t i=0;i<rows.size();++i){const auto& q=rows[i];
        if(q.first<p.origin || q.end<=q.first || q.rate!=p.rate || q.generation!=p.generation ||
            !(q.complex_noise_variance>0) || !std::isfinite(q.value.real()) || !std::isfinite(q.value.imag()) ||
            (i && rows[i-1].end!=q.first))return out;
        times[i]=(static_cast<double>(q.first-p.origin)+.5*(q.end-q.first-1))/q.rate;
        noise[i]=q.complex_noise_variance;sw+=1/noise[i];st+=times[i]/noise[i];
        values[i]=q.value;energy+=std::norm(values[i])/noise[i];
    }
    const auto center=st/sw;
    // The independent FLL domain must fit inside one coherent main lobe.
    // This is a bounded local synchronous fit, never a PCM/history search.
    if(!(p.fll_uncertainty>=0) || p.fll_uncertainty*duration>=.4)return out;
    const auto last_time=times.back();
    const auto f0=p.fll_hz-p.fll_slope*(last_time-center)-p.reference_hz;
    const auto radius=.4/duration;
    std::array<std::complex<double>,context_size> base{};
    for(std::size_t i=0;i<rows.size();++i){times[i]-=center;
        base[i]=values[i]*std::polar(1.,-tau*(f0*times[i]+.5*p.fll_slope*times[i]*times[i]));}
    double correction=0;std::complex<double> b{},db{},ddb{};
    const auto project=[&](double f){b=db=ddb={};for(std::size_t i=0;i<rows.size();++i){
        const auto v=base[i]*std::polar(1.,-tau*f*times[i])/noise[i];
        b+=v;db+=v*std::complex<double>{0,-tau*times[i]};ddb-=v*(tau*tau*times[i]*times[i]);}};
    for(unsigned iteration=0;iteration<4;++iteration){project(correction);
        const auto gradient=2*std::real(db*std::conj(b));
        const auto curvature=2*(std::real(ddb*std::conj(b))+std::norm(db));
        if(!(curvature<0))return out;
        const auto delta=std::clamp(-gradient/curvature,-.5*radius,.5*radius);
        correction=std::clamp(correction+delta,-radius,radius);
    }
    project(correction);
    const auto final_gradient=2*std::real(db*std::conj(b));
    const auto final_curvature=2*(std::real(ddb*std::conj(b))+std::norm(db));
    const auto convergence_tolerance=1e-4/duration;
    // This is the BROAD coherent main-lobe edge, not the old narrow FLL
    // interval. A clipped or unconverged local fit is not a measured maximum.
    if(!(final_curvature<0) || std::abs(correction)+convergence_tolerance>=radius ||
       std::abs(final_gradient/final_curvature)>convergence_tolerance)return out;
    const auto amplitude=b/sw;
    // Account for the fitted amplitude's noise bias in aggregate SNR only.
    out.snr=std::max(0.,std::norm(amplitude)*sw-1.);
    out.chi=std::max(0.,energy-std::norm(b)/sw);
    // Noise-normalized residual: N complex rows minus complex amplitude
    // and frequency give 2*N-3 real residual degrees of freedom.
    // Brownian phase variation is separately allowed by its full-context bound.
    const auto process_residual=2*std::norm(amplitude)*sigma*sigma*duration*sw;
    if(out.snr<16 || out.chi>20+process_residual)return out;
    const auto covariance=joint_covariance(times,noise,std::norm(amplitude),sigma,duration);
    if(!covariance.valid)return out;
    out.phase=std::arg(amplitude)/tau;out.seconds=center;out.variance=covariance.phase_variance;
    out.hz=p.reference_hz+f0+correction;out.frequency_sigma=std::sqrt(covariance.frequency_variance);
    out.thermal_phase_variance=covariance.thermal_phase_variance;
    out.process_phase_variance=covariance.process_phase_variance;
    out.thermal_frequency_variance=covariance.thermal_frequency_variance;
    out.process_frequency_variance=covariance.process_frequency_variance;
    out.valid=true;return out;
}
struct CycleBridge {
    double first_seconds=0,seconds=0,expected=0,raw=0,lifted=0,gate=0;
    double old_expected=0,old_gate=0,thermal_variance=0,process_variance=0;
};
// Only disjoint endpoint contexts use the independent thermal sum. Conditional
// centered Fisher phase/frequency covariance is zero; Brownian covariance is
// not discarded. Its endpoint and intervening-gap terms use Minkowski's bound.
inline CycleBridge cycle_bridge(const Anchor& previous,const JointContext& joint,const Projection& p,double sigma)noexcept {
    CycleBridge out;out.first_seconds=previous.seconds;out.seconds=joint.seconds;
    const auto dt=joint.seconds-previous.seconds;
    out.expected=.5*(previous.joint_hz+joint.hz-2*p.reference_hz)*dt;
    out.raw=joint.phase-std::remainder(previous.phase,1.);
    out.lifted=out.raw+std::round(out.expected-out.raw);
    out.thermal_variance=previous.thermal_phase_variance+joint.thermal_phase_variance+
        .25*dt*dt*(previous.thermal_frequency_variance+joint.thermal_frequency_variance);
    const auto process_std=std::sqrt(previous.process_phase_variance)+std::sqrt(joint.process_phase_variance)+
        .5*dt*(std::sqrt(previous.process_frequency_variance)+std::sqrt(joint.process_frequency_variance))+
        sigma*std::sqrt(dt)/tau;
    out.process_variance=process_std*process_std;
    out.gate=3*std::sqrt(out.thermal_variance+out.process_variance)+.25*dt*dt;
    const auto end_seconds=(static_cast<double>(p.first-p.origin)+.5*(p.end-p.first-1))/p.rate;
    const auto current_hz=p.fll_hz-p.fll_slope*(end_seconds-joint.seconds);
    out.old_expected=(current_hz-p.reference_hz)*dt-.5*p.fll_slope*dt*dt;
    out.old_gate=3*std::sqrt(joint.variance+previous.variance+2*sigma*sigma*dt/(tau*tau))+
        p.fll_uncertainty*dt+.25*dt*dt;
    return out;
}
struct Runtime {
    std::array<Anchor,capacity> anchors{};
    std::array<Projection,context_size> context{};
    std::size_t used=0,context_used=0,admitted=0,rejected=0,resets=0;
    std::size_t contexts=0,joint_passes=0,ambiguities=0,model_resets=0,purity_rejects=0;
    std::uint64_t generation=0,bridge_serial=0;
    CycleBridge last_bridge{};
    double reference_hz=0;
    bool current=false;
    void reset()noexcept {used=context_used=0;current=false;++resets;}
    void observe(const Projection& p,double sigma,bool oracle_clean=false,bool oracle_mode=false)noexcept {
        if(generation!=p.generation){reset();generation=p.generation;}
        current=p.qualified;
        std::size_t retained=0;
        for(std::size_t i=0;i<used;++i)
            if(p.end>=anchors[i].first && p.end-anchors[i].first<=36ULL*p.rate)anchors[retained++]=anchors[i];
        used=retained;
        if(!p.rate || p.end<=p.first || !(p.complex_noise_variance>0)) {context_used=0;current=false;return;}
        if(context_used && context[context_used-1].end!=p.first)context_used=0;
        if(context_used==context.size()){std::move(context.begin()+1,context.end(),context.begin());--context_used;}
        context[context_used++]=p;
        if(context_used<context.size() || !current)return;
        ++contexts;
        const auto joint=joint_context(context,sigma);
        if(!joint.valid){
            // A disturbed projection context does not erase a measured
            // frequency reference. Independent current FLL qualification
            // and the estimator's <=0.5 s actual-anchor age still apply.
            ++purity_rejects;return;
        }
        ++joint_passes;
        const auto seconds=joint.seconds,variance=joint.variance;
        if(oracle_mode && !oracle_clean)return;
        if(used && (reference_hz!=p.reference_hz || seconds-anchors[used-1].seconds>1.)){
            used=0;++resets;
        }
        reference_hz=p.reference_hz;
        auto phase=joint.phase;
        if(used) {
            const auto& previous=anchors[used-1];const auto dt=seconds-previous.seconds;
            if(dt<=0)return;
            const auto prior=phase_detail::estimate(std::span(anchors).first(used),previous.seconds,sigma,reference_hz);
            if(prior.valid && std::abs(joint.hz-(prior.hz+prior.slope*dt))>
                prior.uncertainty+prior.slope_uncertainty*dt+3*joint.frequency_sigma){
                used=0;current=false;++rejected;++resets;++model_resets;return;
            }
            const auto context_width=static_cast<double>(context.back().end-context.front().first)/p.rate;
            if(dt+1e-12<context_width || context.front().first<previous.end)return;
            last_bridge=cycle_bridge(previous,joint,p,sigma);++bridge_serial;
            const auto& bridge=last_bridge;
            if(!(bridge.gate<.45) || std::abs(bridge.lifted-bridge.expected)>bridge.gate){
                ++rejected;++ambiguities;current=false;return;
            }
            const auto lifted=bridge.lifted;
            const auto stochastic=3*std::sqrt(variance+previous.variance+2*sigma*sigma*dt/(tau*tau));
            if(prior.valid) {
                const auto predicted=(prior.hz-reference_hz)*dt+.5*prior.slope*dt*dt;
                const auto bound=stochastic+prior.uncertainty*dt+.5*prior.slope_uncertainty*dt*dt;
                const auto at_center=prior.hz+prior.slope*dt;
                const auto frequency_bound=prior.uncertainty+prior.slope_uncertainty*dt+3*joint.frequency_sigma;
                if(std::abs(lifted-predicted)>bound || std::abs(joint.hz-at_center)>frequency_bound){
                    used=0;current=false;++rejected;++resets;++model_resets;return;
                }
            }
            phase=previous.phase+lifted;
        }
        std::size_t keep=0;for(std::size_t i=0;i<used;++i)if(seconds-anchors[i].seconds<=36.)anchors[keep++]=anchors[i];used=keep;
        if(used==anchors.size()){std::move(anchors.begin()+1,anchors.end(),anchors.begin());--used;}
        anchors[used++]={seconds,phase,variance,context.front().first,context.back().end,
            joint.hz,joint.thermal_phase_variance,joint.process_phase_variance,
            joint.thermal_frequency_variance,joint.process_frequency_variance};++admitted;
    }
    bool fresh(double now)const noexcept {
        return used && now>=anchors[used-1].seconds && now-anchors[used-1].seconds<=.5;
    }
    Estimate estimate(double now,double sigma)const noexcept {
        return current&&fresh(now)?phase_detail::estimate(std::span(anchors).first(used),now,sigma,reference_hz):Estimate{};
    }
};
}
