#include "datapump/helper_startup_timing.hpp"
#include "datapump/helper_carrier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

namespace datapump::helper {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;

double gate(double x) {
    const double t = std::clamp(x, 0., 1.);
    return t*t*t*(10+t*(-15+6*t));
}

struct Score { double time=0, fit=0; };
}

HelperStartupTimingResult estimate_helper_startup_timing(
    std::span<const std::complex<float>> samples,
    const HelperStartupTimingConfig& c) {
    HelperStartupTimingResult out;
    if (!std::isfinite(c.sample_rate) || c.sample_rate < 1000 ||
        !std::isfinite(c.first_sample_seconds) || !std::isfinite(c.search_min_seconds) ||
        !std::isfinite(c.search_max_seconds) || c.search_max_seconds <= c.search_min_seconds ||
        c.search_min_seconds < c.first_sample_seconds ||
        !std::isfinite(c.audio_scale) || c.audio_scale <= 0 ||
        !std::isfinite(c.common_offset_hz) || !std::isfinite(c.high_hz) ||
        !std::isfinite(c.low_hz) || !std::isfinite(c.auxiliary_hz) ||
        c.low_hz <= 0 || c.auxiliary_hz <= c.low_hz || c.high_hz <= c.auxiliary_hz ||
        c.audio_scale*c.high_hz+std::abs(c.common_offset_hz) >= c.sample_rate/2)
        return out;
    // The finite early window ends before auxiliary metadata or punctuation.
    // Bound rate/candidate count explicitly to keep this diagnostic cheap.
    const double desired = std::ceil(.040*c.sample_rate);
    if (!std::isfinite(desired) || desired > 100000 || desired < 32) return out;
    const auto count = static_cast<std::size_t>(desired);
    if (samples.size() < count ||
        c.search_max_seconds+.008/c.audio_scale >
            c.first_sample_seconds+static_cast<double>(count)/c.sample_rate)
        return out;
    const double candidate_count = std::ceil((c.search_max_seconds-c.search_min_seconds)*c.sample_rate);
    if (!std::isfinite(candidate_count) || candidate_count < 1 || candidate_count > 4096) return out;
    const auto steps = static_cast<std::size_t>(candidate_count);
    constexpr std::size_t maximum_sample_products=8000000;
    // One product here means one candidate/template sample pair. The cap is
    // a portable work bound, not a processor-independent time guarantee.
    if (steps+1>maximum_sample_products/count) {out.work_limited=true;return out;}
    const double step = (c.search_max_seconds-c.search_min_seconds)/static_cast<double>(steps);
    std::vector<Complex> values(count), hi(count), lo(count), aux(count), common(count);
    double observed_power = 0;
    for (std::size_t i=0; i<count; ++i) {
        const auto v=samples[i];
        if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) return out;
        values[i]=Complex(v.real(),v.imag()); observed_power+=std::norm(values[i]);
        const double t=c.first_sample_seconds+static_cast<double>(i)/c.sample_rate;
        hi[i]=std::polar(1.,tau*std::remainder(c.audio_scale*c.high_hz*t,1.));
        lo[i]=std::polar(1.,tau*std::remainder(c.audio_scale*c.low_hz*t,1.));
        aux[i]=std::polar(1.,tau*std::remainder(c.audio_scale*c.auxiliary_hz*t,1.));
        common[i]=std::polar(1.,-tau*std::remainder(c.common_offset_hz*t,1.));
        values[i]*=common[i];
    }
    if (!(observed_power > 0) || !std::isfinite(observed_power)) return out;
    const double low_amp=std::sqrt(low_pilot_relative_power);
    const double aux_amp=std::sqrt(auxiliary_relative_power);
    const auto evaluate = [&](double onset,double* model_energy=nullptr) {
        const auto hs=std::polar(1.,-tau*std::remainder(c.audio_scale*c.high_hz*onset,1.));
        const auto ls=std::polar(1.,-tau*std::remainder(c.audio_scale*c.low_hz*onset,1.));
        const auto as=std::polar(1.,-tau*std::remainder(c.audio_scale*c.auxiliary_hz*onset,1.));
        Complex cross{}; double energy=0;
        for (std::size_t i=0; i<count; ++i) {
            const double t=c.first_sample_seconds+static_cast<double>(i)/c.sample_rate;
            const double tx=c.audio_scale*(t-onset);
            if (tx < 0) continue;
            const double start=gate(tx/.004);
            const double pure=c.hopping?start*start:start;
            const double auxiliary=start*gate(tx/.008);
            const Complex model=pure*(hi[i]*hs+low_amp*lo[i]*ls)+auxiliary*aux_amp*aux[i]*as;
            cross+=values[i]*std::conj(model);energy+=std::norm(model);
        }
        if (model_energy) *model_energy=energy;
        return energy>0 ? std::norm(cross)/energy : 0.;
    };
    std::vector<Score> coarse;coarse.reserve(steps+1);
    for (std::size_t i=0;i<=steps;++i) {
        const double t=c.search_min_seconds+step*static_cast<double>(i);
        coarse.push_back({t,evaluate(t)});
    }
    std::size_t local_maxima=0;
    for (std::size_t i=0;i<=steps;++i) {
        if ((i && coarse[i].fit<coarse[i-1].fit) ||
            (i<steps && coarse[i].fit<coarse[i+1].fit)) continue;
        ++local_maxima;
    }
    // A maximum costs at most 19 further template evaluations. Bound this
    // before refinement, including pathological flat/noise-only profiles.
    if (local_maxima>(maximum_sample_products/count-(steps+1))/19) {
        out.work_limited=true;return out;
    }
    std::vector<Score> peaks;peaks.reserve(local_maxima);
    // Refine every coarse local maximum, not only the winning alias. A grid
    // point alone can miss a narrow competing peak at high C/N0.
    for (std::size_t i=0;i<=steps;++i) {
        if ((i && coarse[i].fit<coarse[i-1].fit) ||
            (i<steps && coarse[i].fit<coarse[i+1].fit)) continue;
        if (!i || i==steps) { peaks.push_back(coarse[i]);continue; }
        double a=coarse[i-1].time,b=coarse[i+1].time;
        constexpr double g=.6180339887498948482;
        double x=b-g*(b-a),y=a+g*(b-a),fx=evaluate(x),fy=evaluate(y);
        for (int j=0;j<16;++j) {
            if (fx>fy) {b=y;y=x;fy=fx;x=b-g*(b-a);fx=evaluate(x);}
            else {a=x;x=y;fx=fy;y=a+g*(b-a);fy=evaluate(y);}
        }
        const double t=(a+b)/2;peaks.push_back({t,evaluate(t)});
    }
    if (peaks.empty()) return out;
    const auto best=*std::max_element(peaks.begin(),peaks.end(),
        [](const Score& a,const Score& b){return a.fit<b.fit;});
    const double noise=std::max((observed_power-best.fit)/static_cast<double>(count-1),
                                observed_power/static_cast<double>(count)*1e-16);
    if (!(noise>0) || !std::isfinite(noise) || !std::isfinite(best.fit)) return out;
    out.candidates=coarse.size()+peaks.size();out.onset_seconds=best.time;
    out.evidence=best.fit/noise;
    out.evidence_threshold=std::log(static_cast<double>(out.candidates)*1e6);
    if (!(out.evidence>out.evidence_threshold) || best.time<=c.search_min_seconds+step ||
        best.time>=c.search_max_seconds-step) return out;
    // Compare the common complex transfer with separate complex tone gains,
    // *also searching onset*. Testing flexible gains only at the shared-fit
    // onset misses differential phase that masquerades as another delay.
    // Four additional real parameters and a second onset search are fitted;
    // the threshold includes a candidate-search penalty. This remains an
    // engineering witness, not a proof of arbitrary channel compatibility.
    const double raw_flexible_steps=std::ceil((c.search_max_seconds-c.search_min_seconds)/.000125);
    if (!std::isfinite(raw_flexible_steps) || raw_flexible_steps<1 || raw_flexible_steps>4096) {
        out.work_limited=true;return out;
    }
    const auto flexible_steps=static_cast<std::size_t>(raw_flexible_steps);
    const auto used=steps+2+19*local_maxima;
    const auto available=maximum_sample_products/count;
    // Each flexible sample evaluates nine complex cross products; include the
    // local refinement and an explicit evaluation at the shared best onset.
    if (used>available || flexible_steps+20>(available-used)/9) {
        out.work_limited=true;return out;
    }
    const auto flexible_fit=[&](double onset) {
        std::array<std::array<Complex,4>,3> normal{};
        for (std::size_t i=0;i<count;++i) {
            const double t=c.first_sample_seconds+static_cast<double>(i)/c.sample_rate;
            const double tx=c.audio_scale*(t-onset);
            if (tx<0) continue;
            const double start=gate(tx/.004), pure=c.hopping?start*start:start;
            const double auxiliary=start*gate(tx/.008);
            // Per-tone phase factors depending only on onset are absorbed in
            // the independent complex gains and therefore need not be formed.
            const std::array<Complex,3> column{pure*hi[i],
                pure*low_amp*lo[i],auxiliary*aux_amp*aux[i]};
            for (std::size_t j=0;j<3;++j) {
                normal[j][3]+=std::conj(column[j])*values[i];
                for (std::size_t k=0;k<3;++k)
                    normal[j][k]+=std::conj(column[j])*column[k];
            }
        }
        const std::array<Complex,3> rhs{normal[0][3],normal[1][3],normal[2][3]};
        for (std::size_t col=0;col<3;++col) {
            std::size_t pivot=col;
            for (std::size_t row=col+1;row<3;++row)
                if (std::abs(normal[row][col])>std::abs(normal[pivot][col])) pivot=row;
            if (!(std::abs(normal[pivot][col])>1e-12))
                return std::numeric_limits<double>::quiet_NaN();
            if (pivot!=col) std::swap(normal[pivot],normal[col]);
            const auto divisor=normal[col][col];
            for (std::size_t k=col;k<4;++k) normal[col][k]/=divisor;
            for (std::size_t row=0;row<3;++row) if (row!=col) {
                const auto factor=normal[row][col];
                for (std::size_t k=col;k<4;++k) normal[row][k]-=factor*normal[col][k];
            }
        }
        Complex fit{};
        for (std::size_t i=0;i<3;++i) fit+=std::conj(rhs[i])*normal[i][3];
        return fit.real();
    };
    double fit=flexible_fit(best.time),fit_time=best.time;
    if (!std::isfinite(fit)) return out;
    const double flex_step=(c.search_max_seconds-c.search_min_seconds)/
        static_cast<double>(flexible_steps);
    for (std::size_t i=0;i<=flexible_steps;++i) {
        const double t=c.search_min_seconds+static_cast<double>(i)*flex_step;
        const double v=flexible_fit(t);
        if (!std::isfinite(v)) return out;
        if (v>fit) {fit=v;fit_time=t;}
    }
    double left=std::max(c.search_min_seconds,fit_time-flex_step),
           right=std::min(c.search_max_seconds,fit_time+flex_step);
    constexpr double golden=.6180339887498948482;
    double x=right-golden*(right-left),y=left+golden*(right-left),
           fx=flexible_fit(x),fy=flexible_fit(y);
    for (int i=0;i<14;++i) {
        if (fx>fy) {right=y;y=x;fy=fx;x=right-golden*(right-left);fx=flexible_fit(x);}
        else {left=x;x=y;fx=fy;y=left+golden*(right-left);fy=flexible_fit(y);}
    }
    fit=std::max({fit,fx,fy});
    if (!std::isfinite(fit) || fit<best.fit-1e-7*observed_power) return out;
    const double independent_noise=std::max((observed_power-fit)/static_cast<double>(count-3),
        observed_power/static_cast<double>(count)*1e-16);
    if (!(independent_noise>0) || !std::isfinite(independent_noise)) return out;
    // The shared optimum is refined for 16 golden iterations from a bracket
    // two coarse samples wide, whereas independent complex gains can absorb
    // the remaining phase error exactly. Allow for that numerical displacement
    // before attributing its improvement to a non-flat channel. This is an
    // engineering perturbation allowance using the fitted amplitude and a
    // locally unimodal search bracket, not a universal certified bound, a noise
    // floor, or a claim that arbitrary physical channels satisfy the model.
    double shared_energy=0;
    (void)evaluate(best.time,&shared_energy);
    if (!(shared_energy>0) || !std::isfinite(shared_energy)) return out;
    const double time_error=step*std::pow(.6180339887498948482,16);
    // Quintic smootherstep derivative is at most 1.875; the 4 ms high/low
    // double gate has derivative <= 2*1.875/.004, and the auxiliary's 4/8 ms
    // product has derivative <= 1.875/.004 + 1.875/.008.
    const double pure_gate_rate=(c.hopping?2.:1.)*1.875/.004;
    const double auxiliary_gate_rate=1.875/.004+1.875/.008;
    const double derivative_bound=c.audio_scale*(pure_gate_rate+tau*c.high_hz+
        low_amp*(pure_gate_rate+tau*c.low_hz)+
        aux_amp*(auxiliary_gate_rate+tau*c.auxiliary_hz));
    const double amplitude=std::sqrt(best.fit/shared_energy);
    const double displacement=std::sqrt(static_cast<double>(count))*
        amplitude*derivative_bound*time_error;
    const double residual_norm=std::sqrt(std::max(0.,observed_power-best.fit));
    const double roundoff=128*std::numeric_limits<double>::epsilon()*
        static_cast<double>(count)*observed_power;
    const double allowance=displacement*displacement+2*residual_norm*displacement+roundoff;
    if (!std::isfinite(allowance)) return out;
    out.channel_consistency_score=std::max(0.,fit-best.fit-allowance)/independent_noise;
    out.channel_consistency_threshold=std::max(20.,out.evidence_threshold+
        std::log1p(out.evidence_threshold));
    if (!std::isfinite(out.channel_consistency_score) ||
        out.channel_consistency_score>out.channel_consistency_threshold) return out;
    // A likelihood penalty over all local aliases plus the sampled profile is
    // deliberately conservative. This is an engineering witness, not a
    // calibrated interval or evidence against differential channel phase.
    const double delta=out.evidence_threshold;
    double radius=step;
    const auto retain=[&](const Score& point) {
        if ((best.fit-point.fit)/noise <= delta)
            radius=std::max(radius,std::abs(point.time-best.time)+step);
    };
    for (const auto& point:coarse) retain(point);
    for (const auto& point:peaks) retain(point);
    out.support_radius_seconds=radius;
    const double half_branch=.5/(c.audio_scale*(c.high_hz-c.low_hz));
    out.valid=radius<half_branch && std::isfinite(radius);
    return out;
}
} // namespace datapump::helper
