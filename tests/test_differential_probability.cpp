#include "receiver_probability.hpp"
#include "pattern_differential.hpp"
#include "pattern_drift.hpp"
#include "pattern_correlator_batch.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace datapump::simulation::detail;
namespace {
void check(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
ReceiverProbabilityParameters parameters(unsigned windows=256) {
    ReceiverProbabilityParameters p;
    p.differential_windows=windows;p.differential_window_seconds=1;p.seconds=windows;
    p.differential_weights.assign(windows,1./windows);p.differential_correlations.resize(windows);
    p.noise_dimensions=p.coherent_dimensions=p.section_dimensions=16*windows;
    p.acquisition_threshold=p.continuation_threshold=30;
    p.frequency_step_hz=.25/windows;p.frequency_bin_min=-2;p.frequency_bin_max=2;
    return p;
}
void coverage_and_noise() {
    auto p=parameters();p.signal_energy=0;
    const auto quiet=receiver_probability(p);
    check(quiet.available&&quiet.differential_model&&quiet.trials==4096,"supported local geometry must receive actual trials");
    check(quiet.acquired_correct==0&&quiet.acquired_wrong==0,"ordinary noise must not gain uncharged detector choices");
    check(quiet.acquired_correct_lower==0&&quiet.acquired_correct_upper>0&&quiet.acquired_correct_upper<.001,
          "zero observed successes must retain finite Monte Carlo uncertainty");
    p.requested_trials=512;
    const auto preview=receiver_probability(p);
    check(preview.trials==512&&preview.acquired_correct_upper>quiet.acquired_correct_upper,
          "bounded preview trials must expose their wider sampling uncertainty");
    p.requested_trials=4096;
    p.differential_tail_seconds=.01;
    auto invalid=receiver_probability(p);
    check(!invalid.available&&!invalid.unsupported_reason.empty()&&invalid.trials==0,
          "unsupported partial geometry must not reuse old probabilities");
    p=parameters();p.differential_windows=4097;
    invalid=receiver_probability(p);
    check(!invalid.available&&invalid.trials==0,"unsupported work must remain bounded before sampling");
    p=parameters();p.seconds=std::numeric_limits<double>::quiet_NaN();
    check(!receiver_probability(p).available,"nonfinite symbol duration must be rejected before carrier selection");
    p=parameters();p.differential_correlations[3]={1,0};
    check(!receiver_probability(p).available,"singular alternative templates cannot be silently made independent");
    p=parameters();p.diffusion_degrees=200;
    check(!receiver_probability(p).available,"unresolved intrawindow phase must not advertise a probability");
    p=parameters();p.differential_signal_coefficients.assign(256,{});
    p.differential_signal_coefficients.front()[0]={2,0};
    check(!receiver_probability(p).available,
          "template projections cannot invent more fitted signal energy than the physical waveform contains");
}
void weak_and_drifting() {
    auto p=parameters(1024);p.signal_energy=100;p.diffusion_degrees=.5*180/std::numbers::pi;
    const auto weak=receiver_probability(p);
    check(weak.available&&weak.acquired_correct<.01,
          "weak noisy products must not inherit ideal coherent integration gain");
    p.signal_energy=1024;
    const auto useful=receiver_probability(p);
    std::cout<<"Drifting local model: combined "<<useful.acquired_correct<<", coherent "
             <<useful.coherent_acquired_correct<<", added local admissions "<<useful.differential_acquired_correct<<'\n';
    check(useful.acquired_correct>.5&&useful.differential_acquired_correct>.1&&
          useful.acquired_correct>useful.coherent_acquired_correct+.2,
          "shared noisy local products must resolve an independently useful differential regime");
    p.diffusion_degrees=0;p.signal_energy=1e5;
    const auto strong=receiver_probability(p);
    check(strong.acquired_correct>.999&&strong.acquired_wrong==0&&strong.acquired_correct_lower<1&&strong.acquired_correct_upper==1,
          "strong stable reception must retain a finite-sample lower bound");
}
void coherent_distribution_limit() {
    // A stable, exactly timed, orthogonal pair has a noncentral complex-beta
    // coherent fit. Draw that lower-dimensional distribution independently;
    // this reference neither creates local products nor calls the estimator.
    // At this energy each local window is too weak to contribute detectable
    // products, but the added detector's log(2) choice cost must remain.
    auto p=parameters();p.signal_energy=32;p.sections=false;
    p.frequency_bin_min=p.frequency_bin_max=0;
    const auto modeled=receiver_probability(p);
    std::mt19937_64 random(0xd39b416ca5732e10ULL);
    std::normal_distribution<double> normal;
    std::gamma_distribution<double> remainder(p.noise_dimensions-2,1);
    constexpr unsigned count=32768;unsigned acquired=0,coherent=0;
    for(unsigned i=0;i<count;++i) {
        const std::complex<double> a{std::sqrt(p.signal_energy)+normal(random)/std::sqrt(2.),normal(random)/std::sqrt(2.)};
        const std::complex<double> b{normal(random)/std::sqrt(2.),normal(random)/std::sqrt(2.)};
        const auto energy=std::norm(a)+std::norm(b)+remainder(random);
        const auto x=datapump::modem::detail::drift_evidence(std::norm(a),energy,p.noise_dimensions,1,false);
        const auto y=datapump::modem::detail::drift_evidence(std::norm(b),energy,p.noise_dimensions,1,false);
        coherent+=x>=p.acquisition_threshold&&x-y>=1;
        acquired+=x>=p.acquisition_threshold+std::numbers::ln2&&x-y>=1;
    }
    check(std::abs(modeled.acquired_correct-static_cast<double>(acquired)/count)<.035&&
          std::abs(modeled.coherent_acquired_correct-static_cast<double>(coherent)/count)<.035,
          "joint local observations disagree with the independent noncentral-beta coherent limit");
    check(modeled.acquired_correct<modeled.coherent_acquired_correct,
          "an eligible but weak local branch must retain its detector-selection cost");
}
void sampled_shaped_statistics() {
    // Independently score real, radially limited, pulse-shaped PCM through the
    // production real-Gram fits. Timing and carrier are known here: this
    // isolates the local-observation model from acquisition/state behavior,
    // which the separate full receiver matrix exercises.
    using namespace datapump;
    modem::Config config;config.sample_rate=64;config.bandwidth_hz=32;
    config.carrier_hz=16;config.spreading_factor=4096;config.pulse_shaping=true;config.scramble=false;
    modem::PatternCode code(config,config.stream_epoch);
    const auto total=modem::symbol_sample_count(config),window=std::uint64_t{64};
    check(total/window==256,"shaped statistical fixture lost local eligibility");
    std::array<std::vector<std::complex<double>>,2> baseband,basis;
    for(unsigned bit=0;bit<2;++bit){baseband[bit].resize(total);basis[bit].resize(total);}
    auto p=parameters();p.noise_dimensions=p.coherent_dimensions=p.section_dimensions=static_cast<double>(total)/2;
    p.frequency_bin_min=p.frequency_bin_max=0;p.pulse_shaping=true;
    p.differential_weights.assign(256,0);p.differential_alternative_weights.assign(256,0);
    p.differential_correlations.assign(256,{});p.weights.fill(0);
    p.differential_signal_coefficients.assign(256,{});
    double source_energy=0;
    for(std::uint64_t sample=0;sample<total;++sample) {
        const auto rotation=std::polar(1.,2*std::numbers::pi*config.carrier_hz*static_cast<double>(sample)/config.sample_rate);
        for(unsigned bit=0;bit<2;++bit) {
            baseband[bit][sample]=code.shaped_value(0,bit,static_cast<double>(sample));
            basis[bit][sample]=baseband[bit][sample]*rotation;
        }
        const auto local=sample/window;
        p.differential_weights[local]+=std::norm(baseband[0][sample]);
        p.differential_alternative_weights[local]+=std::norm(baseband[1][sample]);
        p.differential_correlations[local]+=baseband[0][sample]*std::conj(baseband[1][sample]);
        const auto source=modem::pattern_limit_pcm(std::sqrt(2*modem::nominal_signal_power)*baseband[0][sample])/
            std::sqrt(2*modem::nominal_signal_power);
        source_energy+=std::norm(source);
        for(unsigned bit=0;bit<2;++bit)
            p.differential_signal_coefficients[local][bit]+=source*std::conj(baseband[bit][sample]);
    }
    const auto sum0=std::accumulate(p.differential_weights.begin(),p.differential_weights.end(),0.);
    const auto sum1=std::accumulate(p.differential_alternative_weights.begin(),p.differential_alternative_weights.end(),0.);
    for(std::size_t i=0;i<256;++i) {
        p.differential_signal_coefficients[i][0]/=std::sqrt(p.differential_weights[i]*source_energy);
        p.differential_signal_coefficients[i][1]/=std::sqrt(p.differential_alternative_weights[i]*source_energy);
        p.differential_correlations[i]/=std::sqrt(p.differential_weights[i]*p.differential_alternative_weights[i]);
        p.differential_weights[i]/=sum0;p.differential_alternative_weights[i]/=sum1;
        p.weights[i/64]+=p.differential_weights[i];
    }
    const auto add=[](modem::detail::CorrelationFit& fit,std::complex<double> basis_value,double sample) {
        const auto c=basis_value.real(),s=basis_value.imag();
        fit.xc+=sample*c;fit.xs+=sample*s;fit.cc+=c*c;fit.ss+=s*s;fit.cs+=c*s;
        fit.energy+=sample*sample;++fit.count;
    };
    constexpr unsigned captures=128;
    for(const auto fixture:std::array{std::array{40.,0.},std::array{500.,25.}}) {
        // Nominal PCM power is an ensemble mean. This finite waveform and its
        // actual transmitter limiter have separately measurable total/fitted
        // energies; preserve the unmatched remainder in the denominator.
        p.signal_energy=fixture[0]*source_energy/static_cast<double>(total);
        p.diffusion_degrees=fixture[1];
        const auto prediction=receiver_probability(p);
        check(prediction.available,"resolved shaped local statistics were rejected");
        std::mt19937_64 noise_random(0x128c3a059bde76ULL),phase_random(0xe7abc4217ULL);
        std::normal_distribution<double> noise_normal,phase_normal;
        const auto sigma=std::sqrt(modem::nominal_signal_power*static_cast<double>(total)/(2*fixture[0]));
        const auto phase_step=p.diffusion_degrees*std::numbers::pi/(180*std::sqrt(config.sample_rate));
        unsigned admitted=0;
        for(unsigned trial=0;trial<captures;++trial) {
            std::array<modem::detail::CorrelationFit,2> whole{},local{};
            std::array<std::array<modem::detail::CorrelationFit,4>,2> quarters{};
            std::array<modem::detail::DifferentialAccumulator,2> differential{};
            double phase=0;
            for(std::uint64_t sample=0;sample<total;++sample) {
                phase+=phase_step*phase_normal(phase_random);
                const auto source=modem::pattern_limit_pcm(std::sqrt(2*modem::nominal_signal_power)*baseband[0][sample]);
                const auto carrier=std::polar(1.,2*std::numbers::pi*config.carrier_hz*static_cast<double>(sample)/config.sample_rate+phase);
                const auto observed=(source*carrier).real()+sigma*noise_normal(noise_random);
                for(unsigned bit=0;bit<2;++bit) {
                    add(whole[bit],basis[bit][sample],observed);
                    add(quarters[bit][sample/(total/4)],basis[bit][sample],observed);
                    add(local[bit],basis[bit][sample],observed);
                    if((sample+1)%window==0) {
                        const auto& fit=local[bit];
                        differential[bit].add(modem::detail::differential_whiten(fit.xc,fit.xs,fit.cc,fit.ss,fit.cs),
                            sample/window,total,window);
                        local[bit]={};
                    }
                }
            }
            std::array<double,2> scores{};
            for(unsigned bit=0;bit<2;++bit) {
                double fitted=0,strongest=0;
                for(const auto& section:quarters[bit]){const auto x=section.explained();fitted+=x;strongest=std::max(strongest,x);}
                const auto older=modem::detail::combine_drift_evidence(whole[bit].score(),
                    modem::detail::drift_evidence(fitted-strongest,whole[bit].energy,static_cast<double>(total),4,true),4);
                scores[bit]=modem::detail::combine_differential_evidence(older,differential[bit].score(),true);
            }
            admitted+=scores[0]>=p.acquisition_threshold&&scores[0]-scores[1]>=1;
        }
        const auto observed=static_cast<double>(admitted)/captures;
        std::cout<<"Shaped sampled statistic E="<<p.signal_energy<<" diffusion="<<p.diffusion_degrees
                 <<": model "<<prediction.acquired_correct<<", observed "<<admitted<<'/'<<captures<<'\n';
        const auto uncertainty=3*std::sqrt(prediction.acquired_correct*(1-prediction.acquired_correct)/captures);
        check(std::abs(observed-prediction.acquired_correct)<=.07+uncertainty,
              "local model disagrees with independently sampled shaped real-Gram scores");
    }
}
}
int main() {
    try {coverage_and_noise();coherent_distribution_limit();weak_and_drifting();sampled_shaped_statistics();std::cout<<"differential probability tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"differential probability tests failed: "<<error.what()<<'\n';return 1;}
}
