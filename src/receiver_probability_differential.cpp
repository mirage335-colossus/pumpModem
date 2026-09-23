#include "receiver_probability.hpp"
#include "probability_random.hpp"
#include "pattern_differential.hpp"
#include "pattern_drift.hpp"
#include "datapump/correlation_experiment.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>
#include <random>
#include <vector>

namespace datapump::simulation::detail {
namespace {
using Complex=std::complex<double>;
constexpr double tau=2*std::numbers::pi;

ReceiverProbability unsupported(const char* why) {
    ReceiverProbability result;result.available=false;result.differential_model=true;
    result.unsupported_reason=why;return result;
}

struct LocalDraw {
    Complex correct,wrong;
};
struct Evidence {
    double coherent=0,older=0,combined=0;
};
std::array<Evidence,2> evaluate(const ReceiverProbabilityParameters& p,
                              const std::vector<LocalDraw>& observations,
                              const std::array<std::array<double,4>,2>& quarter_weights,
                              const std::array<std::vector<double>,2>& aggregation_weights,
                              const std::array<modem::detail::DifferentialAccumulator,2>& differential,
                              double energy,int bin) {
    std::array<std::array<Complex,4>,2> section{};
    const auto angle=-tau*bin*p.frequency_step_hz*p.differential_window_seconds;
    auto rotation=std::polar(1.,angle/2);
    const auto step=std::polar(1.,angle);
    for(std::size_t index=0;index<observations.size();++index) {
        const auto quarter=index/(observations.size()/4);
        const std::array local{observations[index].correct*rotation,observations[index].wrong*rotation};
        for(unsigned bit=0;bit<2;++bit) {
            section[bit][quarter]+=aggregation_weights[bit][index]*local[bit];
        }
        rotation*=step;
    }
    std::array<Evidence,2> result{};
    for(unsigned bit=0;bit<2;++bit) {
        Complex whole{};double fitted=0,strongest=0;
        for(unsigned quarter=0;quarter<4;++quarter) {
            whole+=std::sqrt(quarter_weights[bit][quarter])*section[bit][quarter];
            const auto fit=std::norm(section[bit][quarter])/p.noise_condition;
            fitted+=fit;strongest=std::max(strongest,fit);
        }
        auto& score=result[bit];
        score.coherent=modem::detail::drift_evidence(std::norm(whole)/p.noise_condition,
            energy,p.coherent_dimensions,1,false);
        const auto quarters=modem::detail::drift_evidence(fitted-strongest,
            energy,p.section_dimensions,4,false);
        score.older=modem::detail::combine_drift_evidence(score.coherent,quarters,p.sections?4:1);
        // Adjacent local-window products acquire the same carrier rotation,
        // regardless of their index. Reusing this exact identity avoids a
        // second traversal of all products for each finite-bank hypothesis.
        auto rotated=differential[bit];
        for(auto& sum:rotated.sums)sum*=step;
        score.combined=modem::detail::combine_differential_evidence(score.older,rotated.score(),true);
    }
    return result;
}

double gamma_remainder(double shape,double draw) {
    const auto base=std::max(0.,1-1/(9*shape)+draw/(3*std::sqrt(shape)));
    return shape*base*base*base;
}
}

ReceiverProbability differential_receiver_probability(const ReceiverProbabilityParameters& p) {
    const auto windows=p.differential_windows;
    if(windows<256 || windows>4096 || windows%4 || p.differential_tail_seconds!=0 ||
       !(p.seconds>0) || !std::isfinite(p.seconds) ||
       !(p.differential_window_seconds>0) || !std::isfinite(p.differential_window_seconds) ||
       std::abs(p.seconds/windows-p.differential_window_seconds)>1e-8*p.differential_window_seconds)
        return unsupported("Local model supports 256–4096 complete windows aligned to the four section boundaries.");
    if(p.differential_weights.size()!=windows || p.differential_correlations.size()!=windows)
        return unsupported("Local template weights and correlations are unavailable.");
    if(p.requested_trials<256 || p.requested_trials>4096)
        return unsupported("The probability trial count must be within 256–4096.");
    if(!p.differential_alternative_weights.empty()&&p.differential_alternative_weights.size()!=windows)
        return unsupported("Alternative local template weights are incomplete.");
    if(!p.differential_signal_coefficients.empty()&&p.differential_signal_coefficients.size()!=windows)
        return unsupported("Actual local signal projections are incomplete.");
    if(!std::isfinite(p.signal_energy) || p.signal_energy<0 || !std::isfinite(p.diffusion_degrees) ||
       p.diffusion_degrees<0 || !std::isfinite(p.residual_frequency) ||
       !std::isfinite(p.noise_dimensions) || p.noise_dimensions<=2*windows+5 ||
       !std::isfinite(p.coherent_dimensions) || p.coherent_dimensions<=1 ||
       !std::isfinite(p.section_dimensions) || p.section_dimensions<=4 ||
       !std::isfinite(p.noise_condition) || p.noise_condition<1 || p.noise_condition>1.01 ||
       !(p.timing_coherence>=0&&p.timing_coherence<=1) ||
       !(p.timing_uncertainty_chips>=0&&p.timing_uncertainty_chips<=.5) ||
       !(p.projection_bin_chips>=0&&p.projection_bin_chips<=1) ||
       (p.projection_bin_chips>0&&p.timing_uncertainty_chips>p.projection_bin_chips/2) ||
       !std::isfinite(p.frequency_step_hz) || p.frequency_step_hz<0 ||
       p.frequency_bin_min>0 || p.frequency_bin_max<0 ||
       p.frequency_bin_min < -8194 || p.frequency_bin_max > 8194 ||
       !std::isfinite(p.acquisition_threshold) || p.acquisition_threshold<0 ||
       !std::isfinite(p.continuation_threshold) || p.continuation_threshold<0)
        return unsupported("Local matched-noise geometry is outside the supported model range.");
    std::array<double,4> weights{},alternative_weights{};
    for(std::size_t i=0;i<windows;++i) {
        if(!(p.differential_weights[i]>0) || !std::isfinite(p.differential_weights[i]) ||
           !std::isfinite(p.differential_correlations[i].real()) ||
           !std::isfinite(p.differential_correlations[i].imag()) ||
           std::norm(p.differential_correlations[i])>=1)
            return unsupported("Local template geometry is singular or nonfinite.");
        weights[i/(windows/4)]+=p.differential_weights[i];
        const auto alternative=p.differential_alternative_weights.empty()?
            p.differential_weights[i]:p.differential_alternative_weights[i];
        if(!(alternative>0)||!std::isfinite(alternative))
            return unsupported("Alternative local template geometry is singular or nonfinite.");
        alternative_weights[i/(windows/4)]+=alternative;
    }
    if(std::abs(std::accumulate(weights.begin(),weights.end(),0.)-1)>1e-8)
        return unsupported("Local template weights do not cover the complete symbol.");
    if(std::abs(std::accumulate(alternative_weights.begin(),alternative_weights.end(),0.)-1)>1e-8)
        return unsupported("Alternative local template weights do not cover the complete symbol.");
    for(unsigned quarter=0;quarter<4;++quarter)
        if(std::abs(weights[quarter]-p.weights[quarter])>1e-8)
            return unsupported("Local and whole-section template weights disagree.");
    if(!p.differential_signal_coefficients.empty()) {
        double represented=0;
        for(std::size_t index=0;index<windows;++index) {
            const auto& signal=p.differential_signal_coefficients[index];
            for(const auto value:signal)
                if(!std::isfinite(value.real())||!std::isfinite(value.imag()))
                    return unsupported("Actual local signal projections are nonfinite.");
            const auto correlation=p.differential_correlations[index];
            const auto orthogonal=(signal[1]-correlation*signal[0])/std::sqrt(1-std::norm(correlation));
            represented+=std::norm(signal[0])+std::norm(orthogonal);
        }
        if(represented>1+1e-8)
            return unsupported("Actual local signal projections exceed the received signal energy.");
    }
    const auto sigma=p.diffusion_degrees*std::numbers::pi/180;
    const auto variance=sigma*sigma*p.differential_window_seconds;
    // A single local template correlation does not resolve phase-weighted
    // cross-template leakage inside a window. Restrict this approximation to
    // the locally mild diffusion covered by the sampled validation matrix.
    if(variance>.5)
        return unsupported("Phase diffusion within a local window exceeds the resolved model range.");
    if(std::abs(p.residual_frequency)*p.differential_window_seconds>.05)
        return unsupported("Residual carrier rotation within local windows exceeds the resolved model range.");
    // The midpoint Brownian quadrature is corrected to the exact integrated
    // second moment. Its nodes are local to a receiver window, never a fixed
    // fraction of a very long symbol. Coverage also limits unresolved variation
    // of the two template envelopes within a window.
    const auto nodes=static_cast<unsigned>(std::clamp(std::ceil(variance*8),8.,32.));
    const auto interval=p.differential_window_seconds/nodes;
    const auto step_sigma=sigma*std::sqrt(interval);
    const auto phase_step=tau*p.residual_frequency*interval;
    const auto expected=expected_correlation_coherence(p.differential_window_seconds,p.diffusion_degrees,p.residual_frequency);
    double discrete=nodes;
    for(unsigned k=1;k<nodes;++k)
        discrete+=2*(nodes-k)*std::exp(-.5*variance*k/nodes)*std::cos(tau*p.residual_frequency*p.differential_window_seconds*k/nodes);
    discrete/=static_cast<double>(nodes*nodes);
    const auto correction=std::sqrt(std::max(0.,expected)/std::max(expected,discrete));
    ReceiverProbability result;result.differential_model=true;result.trials=p.requested_trials;
    const std::array quarter_weights{weights,alternative_weights};
    std::array<std::vector<double>,2> aggregation_weights;
    std::array<std::vector<Complex>,2> signal_coefficients;
    for(auto& coefficients:signal_coefficients)coefficients.resize(windows);
    for(unsigned bit=0;bit<2;++bit) {
        aggregation_weights[bit].resize(windows);
        const auto& local_weights=bit&&!p.differential_alternative_weights.empty()?
            p.differential_alternative_weights:p.differential_weights;
        for(std::size_t index=0;index<windows;++index)
            aggregation_weights[bit][index]=std::sqrt(local_weights[index]/quarter_weights[bit][index/(windows/4)]);
    }
    for(std::size_t index=0;index<windows;++index) {
        const auto correlation=p.differential_correlations[index];
        const auto signal=p.differential_signal_coefficients.empty()?
            std::array{Complex{std::sqrt(p.differential_weights[index]),0},
                       correlation*std::sqrt(p.differential_weights[index])}:
            p.differential_signal_coefficients[index];
        signal_coefficients[0][index]=std::sqrt(p.signal_energy)*signal[0];
        signal_coefficients[1][index]=std::sqrt(p.signal_energy)*
            (signal[1]-correlation*signal[0])/std::sqrt(1-std::norm(correlation));
    }
    const bool large_bank=p.frequency_bin_max-p.frequency_bin_min+1>17;
    result.frequency_search_approximation=large_bank ||
        std::max(std::abs(p.frequency_bin_min),std::abs(p.frequency_bin_max))*
            p.frequency_step_hz*p.differential_window_seconds>.05;
    std::mt19937_64 generator(0xa653719de920b47cULL);
    ProbabilityNormal normal;
    std::vector<LocalDraw> observations(windows);
    std::vector<int> candidates;candidates.reserve(24);
    for(std::size_t trial=0;trial<p.requested_trials;++trial) {
        const auto timing_offset=p.timing_uncertainty_chips*probability_uniform(generator);
        auto timing_amplitude=1-timing_offset;
        if(p.pulse_shaping&&timing_offset>1e-9) {
            const auto angle=std::numbers::pi*timing_offset,beta=modem::pattern_pulse_rolloff;
            timing_amplitude=std::sin(angle)/angle*std::cos(beta*angle)/
                (1-4*beta*beta*timing_offset*timing_offset);
        }
        const auto timing=p.timing_coherence*timing_amplitude*timing_amplitude;
        const auto timing_scale=std::sqrt(timing);
        const auto projected_energy=p.signal_energy*(!p.pulse_shaping&&p.projection_bin_chips>0?
            1-2*timing_offset*(1-timing_offset/p.projection_bin_chips):1);
        double phase=0,represented=0,energy=0,slope_sum=0,time_square=0;
        std::array<modem::detail::DifferentialAccumulator,2> differential{};
        for(std::size_t i=0;i<windows;++i) {
            Complex integrated{};
            // Half steps at both ends join successive local windows into one
            // continuous Brownian path. There are no independent phase resets.
            if(variance==0) {
                integrated=std::polar(std::sqrt(expected),phase+phase_step*nodes/2);
                phase+=phase_step*nodes;
                // Keep the additive-noise draws common with the diffusing
                // case when an operator moves the phase setting through zero.
                for(unsigned node=0;node<=nodes;++node)(void)normal(generator);
            } else {
                phase+=step_sigma/std::sqrt(2.)*normal(generator)+phase_step/2;
                for(unsigned node=0;node<nodes;++node) {
                    integrated+=std::polar(1.,phase);
                    if(node+1<nodes)phase+=step_sigma*normal(generator)+phase_step;
                }
                phase+=step_sigma/std::sqrt(2.)*normal(generator)+phase_step/2;
                integrated*=correction/nodes;
            }
            const auto time=(static_cast<double>(i)+.5)/windows-.5;
            slope_sum+=time*phase;time_square+=time*time;
            const auto signal=signal_coefficients[0][i]*timing_scale*integrated;
            const auto orthogonal_signal=signal_coefficients[1][i]*timing_scale*integrated;
            const Complex noise{normal(generator)/std::sqrt(2.),normal(generator)/std::sqrt(2.)};
            const Complex other{normal(generator)/std::sqrt(2.),normal(generator)/std::sqrt(2.)};
            observations[i].correct=signal+noise;
            const auto correlation=p.differential_correlations[i];
            observations[i].wrong=correlation*observations[i].correct+
                std::sqrt(1-std::norm(correlation))*(orthogonal_signal+other);
            differential[0].add(observations[i].correct,i,windows,1);
            differential[1].add(observations[i].wrong,i,windows,1);
            represented+=std::norm(signal)+std::norm(orthogonal_signal);
            energy+=std::norm(observations[i].correct)+std::norm(orthogonal_signal+other);
        }
        const Complex residual{std::sqrt(std::max(0.,projected_energy-represented))+normal(generator)/std::sqrt(2.),
            normal(generator)/std::sqrt(2.)};
        energy+=std::norm(residual)+gamma_remainder(p.noise_dimensions-2*windows-1,normal(generator));
        candidates.clear();
        const auto add=[&](int bin) {
            if(bin<p.frequency_bin_min || bin>p.frequency_bin_max)return;
            if(std::abs(static_cast<double>(bin)*p.frequency_step_hz*p.differential_window_seconds)>.05)return;
            if(std::find(candidates.begin(),candidates.end(),bin)==candidates.end())candidates.push_back(bin);
        };
        if(!large_bank) {
            for(int bin=p.frequency_bin_min;bin<=p.frequency_bin_max;++bin)add(bin);
        } else {
            for(int bin=-8;bin<=8;++bin)add(bin);
            if(p.frequency_step_hz>0) {
                const auto slope=slope_sum/time_square/(tau*p.frequency_step_hz*p.seconds);
                const auto center=static_cast<int>(std::round(std::clamp(slope,
                    static_cast<double>(p.frequency_bin_min),static_cast<double>(p.frequency_bin_max))));
                for(int bin=center-2;bin<=center+2;++bin)add(bin);
            }
        }
        result.frequency_candidates=std::max(result.frequency_candidates,candidates.size());
        std::array<Evidence,2> combined{},coherent{},older{};
        double combined_best=-1,coherent_best=-1,older_best=-1;
        for(const auto bin:candidates) {
            const auto scores=evaluate(p,observations,quarter_weights,aggregation_weights,differential,energy,bin);
            const auto a=std::max(scores[0].combined,scores[1].combined);
            const auto b=std::max(scores[0].coherent,scores[1].coherent);
            const auto c=std::max(scores[0].older,scores[1].older);
            if(a>combined_best){combined_best=a;combined=scores;}
            if(b>coherent_best){coherent_best=b;coherent=scores;}
            if(c>older_best){older_best=c;older=scores;}
        }
        const auto accepted=[](double a,double b,double threshold){return a>=threshold&&a-b>=1;};
        const auto acquired=accepted(combined[0].combined,combined[1].combined,p.acquisition_threshold);
        result.acquired_correct+=acquired;
        result.acquired_wrong+=accepted(combined[1].combined,combined[0].combined,p.acquisition_threshold);
        result.retained_correct+=accepted(combined[0].combined,combined[1].combined,p.continuation_threshold);
        result.retained_wrong+=accepted(combined[1].combined,combined[0].combined,p.continuation_threshold);
        result.coherent_acquired_correct+=accepted(coherent[0].coherent,coherent[1].coherent,p.acquisition_threshold);
        result.coherent_acquired_wrong+=accepted(coherent[1].coherent,coherent[0].coherent,p.acquisition_threshold);
        result.coherent_retained_correct+=accepted(coherent[0].coherent,coherent[1].coherent,p.continuation_threshold);
        result.coherent_retained_wrong+=accepted(coherent[1].coherent,coherent[0].coherent,p.continuation_threshold);
        result.differential_acquired_correct+=acquired&&!accepted(older[0].older,older[1].older,p.acquisition_threshold);
    }
    for(auto* value:{&result.acquired_correct,&result.acquired_wrong,&result.retained_correct,&result.retained_wrong,
            &result.coherent_acquired_correct,&result.coherent_acquired_wrong,&result.coherent_retained_correct,
            &result.coherent_retained_wrong,&result.differential_acquired_correct})*value/=static_cast<double>(p.requested_trials);
    return result;
}
} // namespace datapump::simulation::detail
