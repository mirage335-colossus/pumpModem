#include "receiver_probability.hpp"
#include "estimate_cancellation.hpp"
#include "probability_random.hpp"
#include "pattern_drift.hpp"
#include "datapump/correlation_experiment.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

namespace datapump::simulation::detail {
namespace {
using Complex=std::complex<double>;
constexpr std::size_t trials=4096, nodes=32, phase_draws=4*(nodes+1), draws=phase_draws+24;
constexpr double tau=2*std::numbers::pi;
using Draw=std::array<double,draws>;
const std::vector<Draw>& random_draws() {
    // Fixed draws avoid flickering estimates and keep noise common when an
    // operator changes power, clocks, or message length. No simulation seed
    // or user waveform key selects a lucky estimator run.
    static const auto values=[] {
        std::vector<Draw> result(trials);
        std::mt19937_64 generator(0xe8472ac06b195d3fULL);
        ProbabilityNormal normal;
        for(auto& row:result)for(auto& value:row)value=normal(generator);
        return result;
    }();
    return values;
}
const std::vector<std::array<double,56>>& projection_draws() {
    // Independent fixed draws leave every existing circular-model trial intact.
    static const auto values=[] {
        std::vector<std::array<double,56>> result(trials);
        std::mt19937_64 generator(0x937aa492360218abULL);ProbabilityNormal normal;
        for(auto& row:result)for(auto& value:row)value=normal(generator);
        return result;
    }();
    return values;
}
Complex average_exponential(Complex z) {
    if(std::abs(z)<.01) {
        Complex term{1,0},sum=term;
        for(unsigned k=1;k<14;++k){term*=-z/static_cast<double>(k+1);sum+=term;}
        return sum;
    }
    return (1.-std::exp(-z))/z;
}
struct PhaseSample {
    std::array<Complex,4> combined{},coherent{};
};
struct PhaseModel {
    const ReceiverProbabilityParameters& parameters;
    std::array<Complex,4> mean{};
    std::array<std::array<Complex,4>,4> lower{};
    std::array<Complex,4> stable{};
    double step_sigma=0,angle_step=0,correction=1,mixing=0;
    bool wandering=false;
    explicit PhaseModel(const ReceiverProbabilityParameters& p):parameters(p) {
        const auto length=p.seconds/4;
        const auto sigma=p.diffusion_degrees*std::numbers::pi/180;
        const auto x=.5*sigma*sigma*length,y=tau*p.residual_frequency*length;
        const Complex z{x,-y};
        const auto average=average_exponential(z);
        const auto expected=expected_correlation_coherence(length,p.diffusion_degrees,p.residual_frequency);
        for(std::size_t j=0;j<4;++j) {
            mean[j]=std::exp(-z*static_cast<double>(j))*average;
            stable[j]=std::polar(1.,y*static_cast<double>(j))*average_exponential({0,-y});
        }
        wandering=sigma>0;
        if(!wandering)return;
        step_sigma=sigma*std::sqrt(length/nodes);angle_step=y/nodes;
        // A midpoint sum has a finite-node coherence floor. Correct its
        // second moment while it resolves the diffusion; use a mixing-limit
        // integrated-phasor approximation once it no longer does so.
        double discrete=nodes;
        for(std::size_t k=1;k<nodes;++k)
            discrete+=2*static_cast<double>(nodes-k)*std::exp(-x*static_cast<double>(k)/nodes)*
                std::cos(y*static_cast<double>(k)/nodes);
        discrete/=static_cast<double>(nodes*nodes);
        correction=std::sqrt(expected/std::max(expected,discrete));
        mixing=std::clamp((x-8)/8,0.,1.);
        // At strong diffusion, section integrals approach a joint complex
        // Gaussian. Preserve their exact means and Hermitian covariance,
        // including correlation between neighboring sections. Pseudocovariance
        // is neglected only in this mixing regime; this is an approximation.
        std::array<std::array<Complex,4>,4> covariance{};
        for(std::size_t j=0;j<4;++j)for(std::size_t k=0;k<=j;++k) {
            const auto moment=j==k?Complex{expected,0}:
                average*average*std::exp(-z*static_cast<double>(j-k-1));
            covariance[j][k]=moment-mean[j]*std::conj(mean[k]);
            covariance[k][j]=std::conj(covariance[j][k]);
        }
        for(std::size_t j=0;j<4;++j)for(std::size_t k=0;k<=j;++k) {
            auto value=covariance[j][k];
            for(std::size_t m=0;m<k;++m)value-=lower[j][m]*std::conj(lower[k][m]);
            if(j==k)lower[j][k]={std::sqrt(std::max(0.,value.real())),0};
            else if(lower[k][k].real()>1e-15)lower[j][k]=value/lower[k][k].real();
        }
    }
    PhaseSample sample(const Draw& draw) const {
        if(!wandering)return {stable,stable};
        std::array<Complex,4> result{};
        // A fixed stratification variable blends the two approximations
        // smoothly over x=8..16, instead of a discontinuous duration cutoff.
        const auto selector=.5*std::erfc(-draw[phase_draws+23]/std::sqrt(2.));
        if(selector<mixing) {
            for(std::size_t j=0;j<4;++j) {
                result[j]=mean[j];
                for(std::size_t k=0;k<=j;++k)
                    result[j]+=lower[j][k]*Complex{draw[2*k],draw[2*k+1]}/std::sqrt(2.);
                // The actual normalized average of unit phasors cannot exceed
                // one. Excursions are negligible in the mixing regime.
                if(std::norm(result[j])>1)result[j]/=std::abs(result[j]);
            }
            return {result,result};
        }
        std::size_t at=0;double phase=0;
        std::array<Complex,4*nodes> path{};
        double slope_sum=0,time_square=0;
        for(std::size_t j=0;j<4;++j) {
            phase+=step_sigma/std::sqrt(2.)*draw[at++];
            for(std::size_t node=0;node<nodes;++node) {
                const auto angle=phase+angle_step*(static_cast<double>(j*nodes+node)+.5);
                path[j*nodes+node]=std::polar(1.,angle);
                result[j]+=path[j*nodes+node];
                const auto time=(static_cast<double>(j*nodes+node)+.5)/(4*nodes)-.5;
                slope_sum+=time*angle;time_square+=time*time;
                if(node+1<nodes)phase+=step_sigma*draw[at++];
            }
            phase+=step_sigma/std::sqrt(2.)*draw[at++];
            result[j]*=correction/static_cast<double>(nodes);
        }
        PhaseSample selected{result,result};
        const auto& p=parameters;
        if(!(p.frequency_step_hz>0) || p.frequency_bin_min==p.frequency_bin_max)return selected;
        // The real finite carrier bank can follow the linear component of a
        // wandering phase path. Resolve a bounded neighborhood on that same
        // grid, clamped to its actual endpoints. Selecting by signal fit
        // before adding matched noise approximates the adaptive bank search;
        // it does not grant a perfectly tracked phase trajectory.
        const auto slope_bin=slope_sum/time_square/(tau*p.frequency_step_hz*p.seconds);
        const auto center=static_cast<int>(std::round(std::clamp(slope_bin,
            static_cast<double>(p.frequency_bin_min),static_cast<double>(p.frequency_bin_max))));
        const auto quality=[&](const std::array<Complex,4>& candidate) {
            Complex full{};double total=0,strongest=0;
            for(std::size_t j=0;j<4;++j) {
                full+=p.weights[j]*candidate[j];
                const auto fitted=p.weights[j]*std::norm(candidate[j]);
                total+=fitted;strongest=std::max(strongest,fitted);
            }
            const auto energy=p.noise_dimensions+p.signal_energy;
            const auto signal=p.signal_energy*p.timing_coherence/p.noise_condition;
            const auto coherent=modem::detail::drift_evidence(signal*std::norm(full),energy,p.coherent_dimensions,1,false);
            const auto section=modem::detail::drift_evidence(signal*(total-strongest),energy,p.section_dimensions,4,false);
            return std::array{coherent,modem::detail::combine_drift_evidence(coherent,section,p.sections?4:1)};
        };
        auto best=quality(result);
        for(int bin=std::max(p.frequency_bin_min,center-2);bin<=std::min(p.frequency_bin_max,center+2);++bin) {
            if(!bin)continue;
            const auto angle=-tau*bin*p.frequency_step_hz*p.seconds/(4*nodes);
            const auto step=std::polar(1.,angle);
            auto rotation=std::polar(1.,angle/2);
            std::array<Complex,4> candidate{};
            for(std::size_t j=0;j<4;++j) {
                for(std::size_t node=0;node<nodes;++node) {
                    candidate[j]+=path[j*nodes+node]*rotation;rotation*=step;
                }
                candidate[j]*=correction/static_cast<double>(nodes);
            }
            const auto fit=quality(candidate);
            if(fit[0]>best[0]){best[0]=fit[0];selected.coherent=candidate;}
            if(fit[1]>best[1]){best[1]=fit[1];selected.combined=candidate;}
        }
        return selected;
    }
};
double gamma_remainder(double shape,double normal) {
    // Wilson-Hilferty cubing is a bounded approximation to the remaining
    // noise energy, avoiding parameter-dependent RNG work.
    const auto base=std::max(0.,1-1/(9*shape)+normal/(3*std::sqrt(shape)));
    return shape*base*base*base;
}
std::array<double,2> scores(const std::array<Complex,4>& observations,double energy,
                          const ReceiverProbabilityParameters& p,const std::array<double,4>& weights) {
    Complex coherent{};double section_sum=0,strongest=0;
    for(std::size_t j=0;j<4;++j) {
        coherent+=std::sqrt(weights[j])*observations[j];
        const auto fitted=std::norm(observations[j])/p.noise_condition;
        section_sum+=fitted;strongest=std::max(strongest,fitted);
    }
    const auto full=modem::detail::drift_evidence(std::norm(coherent)/p.noise_condition,
        energy,p.coherent_dimensions,1,false);
    const auto section=modem::detail::drift_evidence(section_sum-strongest,
        energy,p.section_dimensions,4,false);
    return {full,modem::detail::combine_drift_evidence(full,section,p.sections?4:1)};
}
ReceiverProbability calculate(const ReceiverProbabilityParameters& p,std::stop_token stop) {
    estimate_detail::check(stop);
    if(!std::isfinite(p.signal_energy)||p.signal_energy<0 || !std::isfinite(p.noise_dimensions) ||
       p.noise_dimensions<16 || !std::isfinite(p.coherent_dimensions) || p.coherent_dimensions<=1 ||
       !std::isfinite(p.section_dimensions) || p.section_dimensions<=4 ||
       !std::isfinite(p.noise_condition) || p.noise_condition<1 ||
       !(p.timing_coherence>=0&&p.timing_coherence<=1) ||
       !(p.timing_uncertainty_chips>=0&&p.timing_uncertainty_chips<=.5) ||
       !(p.projection_bin_chips>=0&&p.projection_bin_chips<=1) ||
       (p.projection_bin_chips>0&&p.timing_uncertainty_chips>p.projection_bin_chips/2) ||
       !std::isfinite(p.frequency_step_hz) || p.frequency_step_hz<0 ||
       p.frequency_bin_min>0 || p.frequency_bin_max<0 ||
       p.frequency_bin_min < -8194 || p.frequency_bin_max > 8194 ||
       !std::isfinite(p.acquisition_threshold) || p.acquisition_threshold<0 ||
       !std::isfinite(p.continuation_threshold) || p.continuation_threshold<0)
        throw Error("invalid receiver probability geometry");
    if(p.requested_trials<256 || p.requested_trials>trials)
        throw Error("receiver probability trial count must be within 256..4096");
    double weight_sum=0,alternative_sum=0;
    const auto& alternative_weights=p.alternative_weights.value_or(p.weights);
    for(std::size_t j=0;j<4;++j) {
        if(!(p.weights[j]>0) || !std::isfinite(p.weights[j]) ||
           !(alternative_weights[j]>0) || !std::isfinite(alternative_weights[j]) ||
           !std::isfinite(p.correlations[j].real()) || !std::isfinite(p.correlations[j].imag()) ||
           std::norm(p.correlations[j])>=1)
            throw Error("invalid receiver probability section");
        weight_sum+=p.weights[j];alternative_sum+=alternative_weights[j];
    }
    if(std::abs(weight_sum-1)>1e-8 || std::abs(alternative_sum-1)>1e-8)throw Error("receiver probability weights must sum to one");
    if(p.section_signal_coefficients) {
        double represented=0;
        for(std::size_t j=0;j<4;++j) {
            const auto& means=(*p.section_signal_coefficients)[j];
            for(const auto mean:means)if(!std::isfinite(mean.real())||!std::isfinite(mean.imag()))
                throw Error("invalid receiver probability signal mean");
            const auto orthogonal=(means[1]-p.correlations[j]*means[0])/
                std::sqrt(1-std::norm(p.correlations[j]));
            represented+=std::norm(means[0])+std::norm(orthogonal);
        }
        if(!std::isfinite(represented)||represented>1+1e-8)
            throw Error("receiver probability signal means exceed physical energy");
    }
    const PhaseModel phase(p);
    std::vector<ReceiverProjectionModel> projection;
    if(p.projected_noise) {
        if(!p.section_signal_coefficients)throw Error("projected noise requires actual source means");
        std::size_t count=0;
        for(const auto& section:*p.projected_noise)count+=section.samples;
        const auto represented_dimensions=static_cast<double>(count);
        if(p.noise_dimensions<represented_dimensions||p.noise_dimensions!=std::floor(p.noise_dimensions)||
           p.noise_dimensions-represented_dimensions>4) {
            ReceiverProbability unavailable;unavailable.available=false;
            unavailable.unsupported_reason="Clock-scaled projected noise dimensions exceed the nominal-window reference (at most four unmatched guard bins)";
            return unavailable;
        }
        for(std::size_t j=0;j<4;++j) {
            auto section=(*p.projected_noise)[j];
            // The work model rounds its worst clock-scaled observation span
            // upward. Retain up to four extra guard bins as unmatched noise;
            // larger clock/window discrepancies remain outside this reference.
            if(j==3)section.samples+=static_cast<std::size_t>(p.noise_dimensions)-count;
            projection.emplace_back(section);
        }
    }
    ReceiverProbability result;result.trials=p.requested_trials;
    // The legacy phase model chooses a small carrier neighborhood by signal
    // fit before matched noise is applied. Do not present that approximation
    // as an exact finite-bank receiver search in shared planner diagnostics.
    result.frequency_search_approximation=p.frequency_step_hz>0&&p.frequency_bin_min!=p.frequency_bin_max;
    for(std::size_t trial=0;trial<p.requested_trials;++trial) {
        estimate_detail::check(stop);
        const auto& draw=random_draws()[trial];
        const auto phasors=phase.sample(draw);
        const auto timing_offset=p.timing_uncertainty_chips*.5*std::erfc(-draw[phase_draws+19]/std::sqrt(2.));
        auto timing_amplitude=1-timing_offset;
        if(p.pulse_shaping&&timing_offset>1e-9) {
            // A matched root-raised-cosine pulse has raised-cosine timing
            // autocorrelation, smoother than an unshaped chip edge.
            const auto angle=std::numbers::pi*timing_offset;
            const auto beta=modem::pattern_pulse_rolloff;
            timing_amplitude=std::sin(angle)/angle*std::cos(beta*angle)/
                (1-4*beta*beta*timing_offset*timing_offset);
        }
        const auto timing=p.timing_coherence*timing_amplitude*timing_amplitude;
        // Boxcar projection loses some edge energy from fractionally shifted
        // unshaped chips before fitting. Phase-spoiled energy within those
        // observations still stays in the denominator below.
        const auto projected_energy=p.signal_energy*(!p.pulse_shaping&&p.projection_bin_chips>0?
            1-2*timing_offset*(1-timing_offset/p.projection_bin_chips):1);
        std::array<ReceiverProjectionDraw,4> projected{};
        if(!projection.empty())for(std::size_t j=0;j<4;++j) {
            std::array<double,14> coordinates{};
            std::copy_n(projection_draws()[trial].begin()+14*j,14,coordinates.begin());
            projected[j]=projection[j].sample(coordinates);
        }
        const auto observe=[&](const std::array<Complex,4>& fit) {
        std::array<Complex,4> correct{},wrong{};
        double represented=0,energy=0;
        for(std::size_t j=0;j<4;++j) {
            if(!projection.empty()) {
                const auto amplitude=std::sqrt(p.signal_energy*timing)*fit[j];
                const auto& means=(*p.section_signal_coefficients)[j];
                correct[j]=amplitude*means[0]+projected[j].dots[0];
                wrong[j]=amplitude*means[1]+projected[j].dots[1];
                energy+=projected[j].energy+2*std::real(amplitude*std::conj(projected[j].dots[2]));
                continue;
            }
            const auto signal=std::sqrt(p.signal_energy*p.weights[j]*timing)*fit[j];
            const Complex noise{draw[phase_draws+4*j]/std::sqrt(2.),draw[phase_draws+4*j+1]/std::sqrt(2.)};
            const Complex other{draw[phase_draws+4*j+2]/std::sqrt(2.),draw[phase_draws+4*j+3]/std::sqrt(2.)};
            const auto correlation=p.correlations[j];
            const auto scale=std::sqrt(1-std::norm(correlation));
            if(p.section_signal_coefficients) {
                const auto amplitude=std::sqrt(p.signal_energy*timing)*fit[j];
                const auto& coefficients=(*p.section_signal_coefficients)[j];
                const auto ma=amplitude*coefficients[0],mb=amplitude*coefficients[1];
                const auto mo=(mb-correlation*ma)/scale;
                correct[j]=ma+noise;
                const auto orthogonal=mo+other;
                wrong[j]=correlation*correct[j]+scale*orthogonal;
                represented+=std::norm(ma)+std::norm(mo);
                energy+=std::norm(correct[j])+std::norm(orthogonal);
            } else {
                correct[j]=signal+noise;
                wrong[j]=correlation*correct[j]+scale*other;
                represented+=std::norm(signal);
                energy+=std::norm(correct[j])+std::norm(other);
            }
        }
        // Unmatched signal remains in the denominator. It cannot become an
        // artificial SNR improvement just because phase drift spoiled its fit.
        if(!projection.empty()) {
            // Exact for fixed section phasors and the supplied projected source.
            // As in the existing phase/timing approximation, spoiled signal
            // stays in the denominator. Its changing projection onto unmatched
            // noise is not resolved by the isolated-symbol reference.
            energy+=projected_energy;
        } else {
            const auto remainder=std::max(0.,projected_energy-represented);
            const Complex residual{std::sqrt(remainder)+draw[phase_draws+16]/std::sqrt(2.),
                draw[phase_draws+17]/std::sqrt(2.)};
            energy+=std::norm(residual)+gamma_remainder(p.noise_dimensions-9,draw[phase_draws+18]);
        }
        const auto a=scores(correct,energy,p,p.weights),b=scores(wrong,energy,p,alternative_weights);
        return std::array{a,b};
        };
        const auto combined=observe(phasors.combined);
        const auto coherent=phasors.coherent==phasors.combined?combined:observe(phasors.coherent);
        const auto a=combined[0],b=combined[1];
        const auto accepted=[&](double x,double y,double threshold) {return x>=threshold&&x-y>=1;};
        result.acquired_correct+=accepted(a[1],b[1],p.acquisition_threshold);
        result.acquired_wrong+=accepted(b[1],a[1],p.acquisition_threshold);
        result.retained_correct+=accepted(a[1],b[1],p.continuation_threshold);
        result.retained_wrong+=accepted(b[1],a[1],p.continuation_threshold);
        result.coherent_acquired_correct+=accepted(coherent[0][0],coherent[1][0],p.acquisition_threshold);
        result.coherent_acquired_wrong+=accepted(coherent[1][0],coherent[0][0],p.acquisition_threshold);
        result.coherent_retained_correct+=accepted(coherent[0][0],coherent[1][0],p.continuation_threshold);
        result.coherent_retained_wrong+=accepted(coherent[1][0],coherent[0][0],p.continuation_threshold);
    }
    const auto sampled=static_cast<double>(p.requested_trials);
    result.acquired_correct/=sampled;result.acquired_wrong/=sampled;
    result.retained_correct/=sampled;result.retained_wrong/=sampled;
    result.coherent_acquired_correct/=sampled;result.coherent_retained_correct/=sampled;
    result.coherent_acquired_wrong/=sampled;result.coherent_retained_wrong/=sampled;
    return result;
}
} // namespace
ReceiverProjectionModel::ReceiverProjectionModel(const ReceiverProjectionSection& section):section_(section) {
    if(section.samples<8||section.samples>262144||
       !(section.variance[0]>0&&section.variance[1]>0)||
       std::abs(section.variance[0]+section.variance[1]-1)>1e-12)
        throw Error("invalid projected noise plane geometry");
    for(unsigned plane=0;plane<2;++plane) {
        auto residual=section.gram[plane];auto& lower=lower_[plane];
        double scale=0;
        for(unsigned i=0;i<6;++i)scale=std::max(scale,std::abs(residual[6*i+i]));
        const auto tolerance=1e-11*std::max(1.,scale);
        for(unsigned i=0;i<6;++i)for(unsigned j=0;j<6;++j)
            if(!std::isfinite(residual[6*i+j])||
               std::abs(residual[6*i+j]-residual[6*j+i])>tolerance)
                throw Error("invalid projected noise Gram symmetry");
        // Pivot on the largest residual diagonal. A tiny leading vector can
        // have a legitimate large cross term with a later vector; unpivoted
        // thresholding would wrongly reject that positive-semidefinite Gram.
        for(unsigned k=0;k<6;++k) {
            unsigned pivot=0;
            for(unsigned i=1;i<6;++i)if(residual[6*i+i]>residual[6*pivot+pivot])pivot=i;
            const auto diagonal=residual[6*pivot+pivot];
            if(diagonal<=tolerance)break;
            for(unsigned i=0;i<6;++i)lower[6*i+k]=residual[6*i+pivot]/std::sqrt(diagonal);
            for(unsigned i=0;i<6;++i)for(unsigned j=0;j<6;++j)
                residual[6*i+j]-=lower[6*i+k]*lower[6*j+k];
            ++rank_[plane];
        }
        for(const auto value:residual)if(std::abs(value)>8*tolerance)
            throw Error("projected noise Gram is not positive semidefinite");
    }
}
ReceiverProjectionDraw ReceiverProjectionModel::sample(const std::array<double,14>& draw) const {
    ReceiverProjectionDraw result;
    for(unsigned plane=0;plane<2;++plane) {
        const auto& lower=lower_[plane];const auto variance=section_.variance[plane];
        std::array<double,6> dots{};double energy=0;
        for(unsigned i=0;i<6;++i) {
            if(i<rank_[plane])energy+=draw[7*plane+i]*draw[7*plane+i];
            for(unsigned j=0;j<rank_[plane];++j)dots[i]+=lower[6*i+j]*draw[7*plane+j];
        }
        for(unsigned i=0;i<3;++i)result.dots[i]+=std::sqrt(variance)*Complex{dots[2*i],dots[2*i+1]};
        // Orthogonal white-plane energy is independent of all retained dots.
        // Same bounded chi-square approximation as the established model,
        // now weighted separately for the two actual covariance eigenvalues.
        const auto shape=(static_cast<double>(section_.samples)-rank_[plane])/2;
        result.energy+=variance*(energy+2*gamma_remainder(shape,draw[7*plane+6]));
    }
    return result;
}
ReceiverProbability receiver_probability(const ReceiverProbabilityParameters& p,std::stop_token stop) {
    estimate_detail::check(stop);
    if((p.section_signal_coefficients||p.projected_noise)&&(p.differential_windows||!p.real_atoms.empty()))
        throw Error("quarter signal means cannot be used with the differential probability model");
    // Repeated one-bit/current-draft estimates reuse exactly the same per-bit
    // probabilities. A tiny thread-local cache is independent of symbol size.
    struct Entry {ReceiverProbabilityParameters parameters;ReceiverProbability result;};
    thread_local std::vector<Entry> cache;
    for(const auto& entry:cache)if(entry.parameters==p)return entry.result;
    auto result=(p.differential_windows||!p.real_atoms.empty())?differential_receiver_probability(p,stop):calculate(p,stop);
    estimate_detail::check(stop);
    // Rejected geometry can contain arbitrarily sized caller-owned vectors.
    // Do not copy those into the bounded cache merely to remember a cheap
    // coverage failure; only supported geometries have bounded model storage.
    if(!result.available)return result;
    if(result.available&&result.trials) {
        const auto interval=[&](double probability) {
            constexpr double z=1.959963984540054;
            const auto n=static_cast<double>(result.trials),denominator=1+z*z/n;
            const auto center=(probability+z*z/(2*n))/denominator;
            const auto radius=z*std::sqrt(probability*(1-probability)/n+z*z/(4*n*n))/denominator;
            return std::array{probability==0?0.:std::max(0.,center-radius),
                              probability==1?1.:std::min(1.,center+radius)};
        };
        const auto acquired=interval(result.acquired_correct),retained=interval(result.retained_correct);
        result.acquired_correct_lower=acquired[0];result.acquired_correct_upper=acquired[1];
        result.retained_correct_lower=retained[0];result.retained_correct_upper=retained[1];
    }
    if(cache.size()==8)cache.erase(cache.begin());
    cache.push_back({p,result});return result;
}
} // namespace datapump::simulation::detail
