#include "appraisal/hf_phase_filter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace appraisal;
using Complex = datapump::triad::v3::Complex;
using Wide = std::complex<long double>;
using Plan = HfPhaseFilterPlan;
constexpr auto tau = 2 * std::numbers::pi_v<long double>;
unsigned checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void near(long double observed, long double expected, long double tolerance, const char* message) {
    if (!(std::abs(observed - expected) <= tolerance))
        std::cerr << message << ": got " << observed << ", expected " << expected << '\n';
    require(std::abs(observed - expected) <= tolerance, message);
}

template<class Function> void rejects(Function&& function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}

struct Input {
    Plan plan;
    std::array<std::vector<Complex>, 2> q;
    std::array<std::vector<double>, 2> energy;

    explicit Input(std::size_t samples) : plan(hf_phase_filter_plan(samples)) {
        for (auto& bit : q) bit.resize(plan.blocks);
        for (auto& bit : energy) bit.resize(plan.blocks);
    }

    HfPhaseFilterReport score(double variance, std::stop_token stop = {},
                              HfPhaseFilterPrior prior = HfPhaseFilterPrior::walk) const {
        return hf_phase_filter(plan, {q[0], q[1]}, {energy[0], energy[1]}, variance, stop, prior);
    }
};

long double sample_center(std::size_t block) {
    return (static_cast<long double>(block) * 128 + 63.5L) / 1920;
}

long double emission(const Input& input, std::size_t bit, std::size_t group,
                     int frequency_bin, long double phase, long double radius, long double variance) {
    long double answer = 0;
    for (std::size_t j = group * 15; j < (group + 1) * 15 && j < input.plan.blocks; ++j) {
        const auto unit = std::polar(1.L, -phase - tau * (sample_center(j) - group) * frequency_bin / 16);
        answer += (2 * radius * std::real(unit * Wide(input.q[bit][j])) -
                   radius * radius * input.energy[bit][j]) / variance;
    }
    return answer;
}

long double log_sum(std::span<const long double> values) {
    const auto maximum = *std::max_element(values.begin(), values.end());
    long double sum = 0;
    for (const auto value : values) sum += std::exp(value - maximum);
    return maximum + std::log(sum);
}

void compare_two_group_enumeration(const Input& input, double variance, long double tolerance) {
    const auto report = input.score(variance);
    const auto sticky = input.score(variance,{},HfPhaseFilterPrior::sticky);
    for (std::size_t bit = 0; bit < 2; ++bit) {
        const auto path = hf_phase_filter_path_diagnostic(input.plan,input.q[bit],input.q[bit],
            input.energy[bit],1e22,variance);
        std::array<long double, 2> mixed{}, sticky_mixed{};
        for (std::size_t r = 0; r < Plan::gain_radii.size(); ++r) {
            const long double radius = Plan::gain_radii[r];
            std::vector<long double> paths, sticky_paths;
            paths.reserve(3 * 33 * 32);
            sticky_paths.reserve(3 * 33 * 32);
            long double raw_max = -std::numeric_limits<long double>::infinity();
            long double weighted_max = raw_max;
            for (int k = -16; k <= 16; ++k)
                for (int p = 0; p < 32; ++p)
                    for (int delta : {-1, 0, 1}) {
                        const auto next = std::min(16, std::max(-16, k + delta));
                        const auto phase = tau * p / 32;
                        const auto next_phase = phase + tau * k / 16;
                        const auto prior = (delta == 0 ? .5L : .25L) / (33 * 32);
                        const auto sticky_prior = (delta == 0 ? 31.L / 32 : 1.L / 64) / (33 * 32);
                        const auto path_log_e = emission(input, bit, 0, k, phase, radius, variance) +
                            emission(input, bit, 1, next, next_phase, radius, variance);
                        paths.push_back(std::log(prior) + path_log_e);
                        sticky_paths.push_back(std::log(sticky_prior) + path_log_e);
                        const long double reflected = (delta == 0 && std::abs(k) == 16) ||
                            (delta != 0 && next == k) ? .75L / (33 * 32) : prior;
                        weighted_max = std::max(weighted_max,std::log(reflected)+path_log_e);
                        raw_max = std::max(raw_max,path_log_e);
                    }
            const auto marginal = log_sum(paths);
            const auto sticky_marginal = log_sum(sticky_paths);
            near(report.radius_log_e[bit][r], marginal, tolerance,
                 "forward result differs from exhaustive path likelihood");
            near(path.maximum_noisy_log_lr[r],raw_max,tolerance,
                 "private unweighted optimum disagrees with exhaustive path enumeration");
            near(path.maximum_prior_weighted_noisy_path_log_e[r],weighted_max,tolerance,
                 "private weighted optimum disagrees with exhaustive path enumeration");
            near(path.conditional_noisy_log_e[r],report.radius_log_e[bit][r],tolerance,
                 "private conditional sum differs from public radius likelihood");
            near(sticky.radius_log_e[bit][r],sticky_marginal,tolerance,
                 "sticky sum differs from exhaustive path likelihood with edge reflection");
            mixed[r] = marginal - std::numbers::ln2_v<long double>;
            sticky_mixed[r] = sticky_marginal - std::numbers::ln2_v<long double>;
        }
        near(report.bit_log_e[bit], log_sum(mixed), tolerance, "fixed radius prior was lost");
        near(sticky.bit_log_e[bit],log_sum(sticky_mixed),tolerance,"sticky fixed radius prior was lost");
    }
}

void two_group_enumeration() {
    Input input(1920 + 2 * 128 + 7);
    require(input.plan.blocks == 18 && input.plan.groups == 2, "partial final group was padded");
    require(input.plan.correlation_products == 2 * 33 * 18 &&
            input.plan.emission_states == 2 * 2 * 33 * 32 * 2 &&
            input.plan.transition_edges == 2 * 2 * 97 * 32, "work quote differs from state lattice");
    for (std::size_t bit = 0; bit < 2; ++bit)
        for (std::size_t j = 0; j < input.plan.blocks; ++j) {
            input.q[bit][j] = std::polar(.3 * (bit + 1), .47 * j - .09 * j * j);
            input.energy[bit][j] = j == input.plan.blocks - 1 ? .02 : .2 + .01 * j;
        }
    compare_two_group_enumeration(input, 1.7, 2e-13L);
}

void dynamic_range() {
    Input input(2 * 1920);
    // The first group favors even phase indices by more than 745 log units.
    // The next favors odd indices even more strongly. The phase transition
    // preserves parity, so an underflowed probability recursion cannot recover.
    for (std::size_t j = 0; j < input.plan.blocks; ++j) {
        input.q[0][j] = j < 15 ? Complex{1e6} : std::polar(1e8, static_cast<double>(tau / 32));
        input.energy[0][j] = input.energy[1][j] = 1.;
    }
    compare_two_group_enumeration(input, 1., 1e-5L);
}

void white_noise_normalization() {
    // Conditional likelihood martingale under H0. Keep the first group's
    // posterior nonuniform and integrate a final one-block group under CN(0,1).
    // Midpoint quadrature over [-6,6] in both normalized real axes.
    Input input(1920 + 17);
    input.q[0][0] = {1.2, -.7};
    input.energy[0][0] = .55;
    const auto prefix = hf_phase_filter(hf_phase_filter_plan(1920),
        {std::span(input.q[0]).first(15), std::span(input.q[1]).first(15)},
        {std::span(input.energy[0]).first(15), std::span(input.energy[1]).first(15)}, 1.);
    input.energy[0].back() = .49;
    constexpr double step = .5;
    std::array<long double,2> integral{};
    for (int i = -12; i < 12; ++i)
        for (int j = -12; j < 12; ++j) {
            const auto x = (i + .5) * step, y = (j + .5) * step;
            input.q[0].back() = std::sqrt(.49) * Complex{x, y};
            const std::array rates{HfPhaseFilterPrior::walk,HfPhaseFilterPrior::sticky};
            for (std::size_t r=0;r<rates.size();++r) {
                const auto log_increment = input.score(1.,{},rates[r]).bit_log_e[0] - prefix.bit_log_e[0];
                integral[r] += std::exp(log_increment - x * x - y * y) * step * step / std::numbers::pi;
            }
        }
    for (const auto value : integral)
        near(value, 1, 2e-12L, "proper-complex H0 predictive likelihood does not have mean one");
}

double fixture(bool changing_frequency) {
    Input input(24 * 1920 - 87);
    int k = changing_frequency ? 13 : -16;
    long double phase = 3 * tau / 32;
    double ideal = 0;
    for (std::size_t g = 0; g < input.plan.groups; ++g) {
        for (std::size_t j = g * 15; j < (g + 1) * 15 && j < input.plan.blocks; ++j) {
            const auto h = std::polar(1.L, phase + tau * k * (sample_center(j) - g) / 16);
            const auto energy = 1.4 + .1 * (j % 7);
            input.q[0][j] = Complex(h) * energy;
            input.energy[0][j] = input.energy[1][j] = energy;
            ideal += energy;
        }
        phase += tau * k / 16;
        if (changing_frequency) {
            if (g < 3) ++k;
            if (g >= 6) --k;
        }
    }
    const auto report = input.score(1.);
    const auto explicit_walk = input.score(1.,{},HfPhaseFilterPrior::walk);
    require(report.bit_log_e==explicit_walk.bit_log_e&&report.radius_log_e==explicit_walk.radius_log_e,
            "explicit walk changed default filter arithmetic");
    const auto score = report.bit_log_e[0];
    require(score < ideal && score > .84 * ideal, "tone/chirp did not retain a charged coherent score");
    require(report.radius_log_e[0][1] > report.radius_log_e[0][0] &&
            report.bit_log_e[1] < 0, "tone/chirp chose smaller radius or wrong bit");
    if (changing_frequency) {
        long double constant = 0;
        for (int k0 = -16; k0 <= 16; ++k0)
            for (int p = 0; p < 32; ++p) {
                const auto phase0 = tau * p / 32;
                long double path = 0;
                for (std::size_t g = 0; g < input.plan.groups; ++g)
                    path += emission(input, 0, g, k0, phase0 + tau * k0 * g / 16, 1., 1.);
                constant = std::max(constant, path);
            }
        require(score > constant + 20, "charged chirp evidence failed to beat even the uncharged constant-tone maximum");
    }
    return score;
}

void rejection_and_missing_data() {
    const auto large = hf_phase_filter_plan(Plan::maximum_samples);
    require(large.blocks == 9000 && large.groups == 600 &&
            large.emission_states * Plan::maximum_timing_atoms < 100'000'000 &&
            large.transition_edges * Plan::maximum_timing_atoms < 300'000'000, "aperture caps changed");
    rejects([] { (void)hf_phase_filter_plan(0); }, "empty aperture accepted");
    rejects([] { (void)hf_phase_filter_plan(Plan::maximum_samples + 1); }, "oversize aperture accepted");
    Input input(3841);
    const auto sticky = hf_phase_filter_transition(HfPhaseFilterPrior::sticky);
    require(sticky.stay==31./32&&sticky.step_each==1./64&&sticky.edge_stay==63./64&&
        sticky.stay+2*sticky.step_each==1.&&sticky.edge_stay+sticky.step_each==1.,
        "sticky fixed prior lost mass at the boundary");
    const auto report = input.score(1.);
    for (double score : report.bit_log_e) near(score, 0, 2e-13L, "zero energy changed H0 evidence");
    const auto sticky_zero = input.score(1.,{},HfPhaseFilterPrior::sticky);
    for (double score : sticky_zero.bit_log_e) near(score,0,2e-13L,"sticky prior lost zero-energy mass");
    rejects([&] { (void)input.score(1.,{},static_cast<HfPhaseFilterPrior>(-1)); },
            "undefined phase prior was accepted");
    rejects([&] { (void)input.score(0); }, "zero noise variance accepted");
    ++input.plan.correlation_products;
    rejects([&] { (void)input.score(1.); }, "modified plan accepted");
    --input.plan.correlation_products;
    input.q[0][0] = 1;
    rejects([&] { (void)input.score(1.); }, "nonzero q for zero energy accepted");
    input.energy[0][0] = 1;
    input.q[0][0] = {std::numeric_limits<double>::infinity(), 0.};
    rejects([&] { (void)input.score(1.); }, "nonfinite q accepted");
    input.q[0][0] = {};
    input.energy[0][0] = -1;
    rejects([&] { (void)input.score(1.); }, "negative energy accepted");
    input.energy[0][0] = 0;
    input.q[1].pop_back();
    rejects([&] { (void)input.score(1.); }, "short input accepted");
    input.q[1].resize(input.plan.blocks);
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { (void)input.score(1., stop.get_token()); }, "cancelled input evaluated");
    const auto paths=[&](double clean_energy, std::stop_token cancellation = {}) {
        return hf_phase_filter_path_diagnostic(input.plan,input.q[0],input.q[1],input.energy[0],clean_energy,
            1.,cancellation);
    };
    rejects([&] { (void)paths(0); }, "clean-energy zero emitted an undefined coherence fraction");
    rejects([&] { (void)paths(1.); }, "zero template energy emitted an undefined coherence fraction");
    input.energy[0][0] = 1.;
    const auto zero = paths(1.);
    near(zero.clean_coherence_power_lower_bound,0,0,"orthogonal zero projection retained clean energy");
    near(zero.maximum_noisy_log_lr[1],-1,1e-14L,"zero noisy correlation lost the norm penalty");
    near(zero.conditional_noisy_log_e[1],-1,1e-14L,"zero noisy correlation summed the wrong prior mass");
    input.q[0][0]=1.;
    const auto aligned = paths(1.);
    near(aligned.clean_coherence_power_lower_bound,1,1e-14L,"aligned clean projection lost coherent energy");
    rejects([&] { (void)paths(.25); }, "impossible clean projection was accepted");
    rejects([&] { (void)paths(1.,stop.get_token()); }, "cancelled private diagnostic evaluated paths");
}

void exhaustive_clean_trace() {
    Input input(1920 + 7 * 128 + 17);
    std::vector<double> center(input.plan.blocks), boundary(input.plan.groups+1);
    for (std::size_t i=0;i<input.plan.blocks;++i) {
        center[i]=.223*sample_center(i)+.016*sample_center(i)*sample_center(i);
        input.energy[0][i]=i+1==input.plan.blocks ? .091 : .35+.03*i;
        input.q[0][i]=std::polar(input.energy[0][i],static_cast<double>(tau)*center[i]);
    }
    for (std::size_t i=0;i<boundary.size();++i) {
        const auto t=static_cast<double>(std::min(i*1920,input.plan.samples))/1920.;
        boundary[i]=.223*t+.016*t*t;
    }
    double energy=0;
    for (double sample:input.energy[0]) energy+=sample;
    const auto old=hf_phase_filter_path_diagnostic(input.plan,input.q[0],input.q[1],input.energy[0],energy,1.);
    const auto diagnostic=hf_phase_filter_path_diagnostic(input.plan,input.q[0],input.q[1],input.energy[0],energy,1.,{},
        HfPhaseFilterCarrierReference{center,boundary});
    require(diagnostic.clean_trace&&old.optimal_clean_correlation_real==diagnostic.optimal_clean_correlation_real&&
        old.clean_coherence_power_lower_bound==diagnostic.clean_coherence_power_lower_bound&&
        old.conditional_noisy_log_e==diagnostic.conditional_noisy_log_e&&old.maximum_noisy_log_lr==diagnostic.maximum_noisy_log_lr&&
        old.maximum_prior_weighted_noisy_path_log_e==diagnostic.maximum_prior_weighted_noisy_path_log_e,
        "clean tracing altered the existing physical diagnostic comparison");
    const auto& trace=*diagnostic.clean_trace;
    near(trace.target_real_correlation,std::sqrt(.99L)*diagnostic.optimal_clean_correlation_real,2e-13L,
        "jump target changed its fixed fraction of best real projection power");
    require(trace.target_reached==(trace.penalized_projection.real_correlation-
        trace.certificate_tolerance_correlation>=trace.target_real_correlation),
        "target witness credits a path that has not cleared its numerical tolerance");
    long double raw_best=0, penalized_best=0;
    std::size_t exact_minimum=2;
    for (int k=-16;k<=16;++k)
        for (int p=0;p<32;++p)
            for (int delta:{-1,0,1}) {
                const auto next=k+delta;
                if (next<-16||next>16) continue;
                const auto phase=tau*p/32;
                const auto clean_real=emission(input,0,0,k,phase,1.,1.)/2+
                    emission(input,0,1,next,phase+tau*k/16,1.,1.)/2+energy/2;
                raw_best=std::max(raw_best,clean_real);
                penalized_best=std::max(penalized_best,clean_real-(delta!=0)*static_cast<long double>(energy)/128);
                if (clean_real>=trace.target_real_correlation)
                    exact_minimum=std::min(exact_minimum,static_cast<std::size_t>(delta!=0));
            }
    near(trace.maximum_projection.real_correlation,raw_best,2e-13L,"traced clean maximum disagrees with independent enumeration");
    near(trace.penalized_objective_correlation,penalized_best,2e-13L,"traced penalized objective disagrees with independent enumeration");
    require(trace.minimum_jumps_lower_bound<=exact_minimum&&exact_minimum<=trace.maximum_projection.jumps,
        "jump certificate contradicts independent exhaustive paths");
    require(!trace.maximum_projection.carrier_frequency_4_group_rms_hz,"short fixture fabricated four complete groups");
    require(trace.workspace_reserved_bytes==hf_phase_filter_clean_trace_workspace(input.plan)&&
        trace.predecessor_labels==2*1056&&trace.analytic_carrier_points==input.plan.blocks+3,
        "clean trace omitted its bounded receipt work");
    rejects([&] { (void)hf_phase_filter_path_diagnostic(input.plan,input.q[0],input.q[1],input.energy[0],energy,1.,{},
        HfPhaseFilterCarrierReference{center,std::span(boundary).first(2)}); },
        "clean trace accepted an incomplete carrier reference");
    require(hf_phase_filter_clean_trace_workspace(hf_phase_filter_plan(Plan::maximum_samples))<=3*1024*1024/2,
        "long private diagnostic exceeds the fixed incremental workspace cap");
}

void analytic_clean_trace() {
    struct Control {double frequency,slope;};
    constexpr std::array controls{Control{.125,0},Control{.15625,0},Control{.140625,0},Control{.015,.012}};
    for (const auto [frequency,slope]:controls) {
        Input input(32*1920);
        const auto cycles=[=](long double t) {return frequency*t+.5L*slope*t*t;};
        std::vector<double> center(input.plan.blocks), boundary(input.plan.groups+1);
        for (std::size_t i=0;i<input.plan.blocks;++i) {
            center[i]=cycles(sample_center(i));
            input.energy[0][i]=1.;
            input.q[0][i]=Complex(std::polar(1.L,tau*cycles(sample_center(i))));
        }
        for (std::size_t i=0;i<boundary.size();++i) boundary[i]=cycles(i);
        const auto r=hf_phase_filter_path_diagnostic(input.plan,input.q[0],input.q[1],input.energy[0],
            static_cast<double>(input.plan.blocks),1.,{},HfPhaseFilterCarrierReference{center,boundary});
        const auto& trace=r.clean_trace.value();
        const auto& path=trace.maximum_projection;
        const auto& penalized=trace.penalized_projection;
        const auto error=trace.certificate_tolerance_correlation;
        require(path.clean_coherence_power>.95&&path.frequency_bins.size()==input.plan.groups,
            "analytic carrier disappeared from the lattice");
        require(path.real_correlation+error>=penalized.real_correlation&&
            trace.penalized_objective_correlation+error>=path.real_correlation-trace.penalty_correlation_units*path.jumps&&
            trace.minimum_jumps_lower_bound<=(trace.target_reached?penalized.jumps:path.jumps),
            "analytic carrier jump certificate disagrees with traced witnesses");
        for (std::size_t i=1;i<path.phase_bins.size();++i)
            require(path.phase_bins[i]==(path.phase_bins[i-1]+2*path.frequency_bins[i-1]+32)%32&&
                std::abs(path.frequency_bins[i]-path.frequency_bins[i-1])<=1,"clean traceback violated frequency or phase transport");
        near(trace.carrier_frequency_total_variation_hz,slope*(input.plan.groups-1),1e-11L,
            "reported carrier variation differs from analytic derivative");
        require(trace.carrier_reversal_count==0,"constant or monotone physical carrier spuriously reverses");
        if (frequency==.125) {
            near(path.carrier_frequency_rms_hz,0,2e-15L,"on-grid tone accrued a frequency error");
            require(path.jumps==0&&penalized.jumps==0&&trace.minimum_jumps_lower_bound==0,
                "on-grid tone paid spurious transition costs");
            near(path.walk_log_prior,-std::log(1056.L)+31*std::log(.5L),2e-13L,"on-grid tone walk prior changed");
            near(path.sticky_log_prior,-std::log(1056.L)+31*std::log(31.L/32),2e-13L,"on-grid tone sticky prior changed");
        } else if (slope==0) {
            // Brute force all zero-jump paths to establish that a phase-grid
            // fit can require jumps even with exactly constant frequency.
            long double no_jump_best=0;
            for (int k=-16;k<=16;++k)
                for (int p=0;p<32;++p) {
                    long double total=0;
                    for (std::size_t g=0;g<input.plan.groups;++g)
                        total+=emission(input,0,g,k,tau*(p/32.L+k*g/16.L),1.,1.)/2+7.5L;
                    no_jump_best=std::max(no_jump_best,total);
                }
            require(no_jump_best<trace.target_real_correlation&&path.jumps>0&&
                path.successive_jump_reversals==path.reversals_below_half_bin_carrier_change&&
                path.reversals_below_half_bin_carrier_change>0,
                "off-grid tone did not discriminate grid motion from physical motion");
            require(path.carrier_frequency_4_group_rms_hz.value()<path.carrier_frequency_rms_hz,
                "short frequency dither did not average toward the clean carrier");
        }
    }
}

HfPhaseFilterReport bridge_score(const HfPreparedPhaseBridge& bridge,const Input& input,double variance=1.) {
    return bridge.score(input.plan,{input.q[0],input.q[1]},{input.energy[0],input.energy[1]},variance);
}

long double bridge_emission(const Input& input,std::size_t bit,std::size_t group,int k,int l,
                            long double phase,long double radius,long double variance) {
    long double result=0;
    for (std::size_t row=60*group;row<60*(group+1)&&row<input.plan.blocks;++row) {
        const auto u=sample_center(row)-4*group;
        const auto rotations=std::polar(1.L,-phase-tau*(k*u+(l-k)*u*u/8)/16);
        result+=(2*radius*std::real(rotations*Wide(input.q[bit][row]))-radius*radius*input.energy[bit][row])/variance;
    }
    return result;
}

void exhaustive_phase_bridge(const HfPreparedPhaseBridge& bridge) {
    Input input(4*1920+3*128+17);
    for (std::size_t bit=0;bit<2;++bit)
        for (std::size_t j=0;j<input.plan.blocks;++j) {
            input.energy[bit][j]=j+1==input.plan.blocks ? .13 : .25+.01*(j%9);
            input.q[bit][j]=std::polar(.27+.04*bit,-.4*j+.028*j*j);
        }
    constexpr long double variance=1.7L;
    const auto actual=bridge_score(bridge,input,variance);
    for (std::size_t bit=0;bit<2;++bit) {
        std::array<long double,2> weighted_radius{};
        for (std::size_t r=0;r<Plan::gain_radii.size();++r) {
            const long double radius=Plan::gain_radii[r];
            std::vector<long double> terms;
            for (int k=-16;k<=16;++k)
                for (int p=0;p<32;++p)
                    for (int d0:{-1,0,1}) {
                        const int l=std::clamp(k+d0,-16,16);
                        const auto phase=tau*p/32;
                        const auto first=bridge_emission(input,bit,0,k,l,phase,radius,variance);
                        for (int d1:{-1,0,1}) {
                            const int m=std::clamp(l+d1,-16,16);
                            // Enumerate duplicate reflected moves separately.
                            const auto prior0=d0==0?.5L:.25L, prior1=d1==0?.5L:.25L;
                            const auto second=bridge_emission(input,bit,1,l,m,phase+tau*(k+l)/8,radius,variance);
                            terms.push_back(first+second+std::log(prior0*prior1/(33*32)));
                        }
                    }
            const auto expected=log_sum(terms);
            near(actual.radius_log_e[bit][r],expected,3e-13L,
                 "four-second bridge disagrees with exhaustive full and partial edges");
            weighted_radius[r]=expected-std::numbers::ln2_v<long double>;
        }
        near(actual.bit_log_e[bit],log_sum(weighted_radius),3e-13L,"bridge radius prior is not normalized");
    }
}

void bridge_noise_and_bounds(const HfPreparedPhaseBridge& bridge) {
    Input empty(8*1920+17);
    const auto null=bridge_score(bridge,empty);
    for (double value:null.bit_log_e) near(value,0.,3e-12L,"bridge zero-energy edge lost prior mass");
    Input one(17);
    one.energy[0][0]=.49;
    long double integral=0;
    constexpr double step=.5;
    for (int i=-12;i<12;++i)
        for (int j=-12;j<12;++j) {
            const auto x=(i+.5)*step,y=(j+.5)*step;
            one.q[0][0]=std::sqrt(.49)*Complex{x,y};
            integral+=std::exp(bridge_score(bridge,one).bit_log_e[0]-x*x-y*y)*step*step/std::numbers::pi;
        }
    near(integral,1.,2e-12L,"bridge proper-complex H0 likelihood does not integrate to one");
    std::stop_source stop;stop.request_stop();
    rejects([&] {const HfPreparedPhaseBridge interrupted(stop.get_token());},"bridge rotators ignored cancellation");
    rejects([&] {bridge.score(one.plan,{one.q[0],one.q[1]},{one.energy[0],one.energy[1]},1.,stop.get_token());},
            "bridge scoring ignored cancellation");
    rejects([&] {bridge_score(bridge,one,0.);},"bridge accepted a zero noise variance");
    ++one.plan.blocks;
    rejects([&] {bridge_score(bridge,one);},"bridge accepted a modified plan");
}

void bridge_coherence(const HfPreparedPhaseBridge& bridge) {
    for (bool off_grid:{false,true}) {
        Input input(24*1920-147);
        double norm=0;
        for (std::size_t j=0;j<input.plan.blocks;++j) {
            const int group=static_cast<int>(j/60);
            const auto u=sample_center(j)-4*group;
            const int k=off_grid ? 2+group%2 : 2+group;
            const int l=off_grid ? 3-group%2 : k+1;
            const auto cycles=off_grid ? 5*sample_center(j)/32 :
                (2*sample_center(j)+sample_center(j)*sample_center(j)/8)/16;
            const auto bridge_cycles=off_grid ? 5*group/8.L+(k*u+(l-k)*u*u/8)/16 : cycles;
            if (off_grid)
                require(std::abs(tau*(cycles-bridge_cycles))<=std::numbers::pi_v<long double>/16,
                    "half-bin tone exceeds independent phase-continuous edge bound");
            input.energy[0][j]=2.;
            input.q[0][j]=Complex(2.L*std::polar(1.L,tau*cycles));
            norm+=input.energy[0][j];
        }
        const auto actual=bridge_score(bridge,input).radius_log_e[0][1];
        const auto path_prior=-std::log(1056.L)-(input.plan.blocks+59)/60*std::log(4.L);
        const auto retained=off_grid ? std::cos(std::numbers::pi_v<long double>/16) : 1.L;
        require(actual+1e-10>=(2*retained-1)*norm+path_prior&&actual<=norm+1e-10,
            "bridge evidence excludes a feasible chirp or half-bin path");
    }
}

void bridge_support_diagnostic() {
    // One full edge and a partial edge, with a planted two-bin ramp. The
    // three independent enumerations are small enough to keep in the test.
    Input input(4*1920+47);
    long double energy=0;
    for (std::size_t j=0;j<input.plan.blocks;++j) {
        const auto t=sample_center(j);
        input.energy[0][j]=j+1==input.plan.blocks ? .17 : .8+.03*(j%7);
        input.q[0][j]=Complex(static_cast<long double>(input.energy[0][j])*std::polar(1.L,tau*(3*t+t*t/4)/16));
        energy+=input.energy[0][j];
    }
    const auto actual=hf_phase_bridge_support_diagnostic(input.plan,input.q[0],input.energy[0],energy);
    const auto exhaustive=[&](int radius,int phase_relaxation) {
        long double max=-std::numeric_limits<long double>::infinity();
        for (int k=-16;k<=16;++k)
            for (int l=std::max(-16,k-radius);l<=std::min(16,k+radius);++l)
                for (int m=std::max(-16,l-radius);m<=std::min(16,l+radius);++m)
                    for (int p=0;p<32;++p)
                        for (int offset=-phase_relaxation;offset<=phase_relaxation;++offset) {
                            const auto start=tau*p/32;
                            const auto end=start+tau*(k+l)/8+tau*offset/32;
                            const auto first=bridge_emission(input,0,0,k,l,start,1.,1.);
                            const auto second=bridge_emission(input,0,1,l,m,end,1.,1.);
                            max=std::max(max,(first+second+energy)/2);
                        }
        return max;
    };
    const auto compare=[&](const HfPhaseBridgeSupportProjection& score,int radius,int phase_relaxation) {
        const auto reference=exhaustive(radius,phase_relaxation);
        near(score.real_correlation,reference,4e-13L,
            "private bridge support differs from independent two-edge path enumeration");
        near(score.clean_coherence_power,(reference/energy)*(reference/energy),5e-14L,
            "private bridge support changed clean-energy normalization");
    };
    compare(actual.original,1,0);
    compare(actual.phase_relaxed,1,1);
    compare(actual.rate_relaxed,2,0);
    require(actual.phase_relaxed.clean_coherence_power>=actual.original.clean_coherence_power&&
            actual.rate_relaxed.clean_coherence_power>actual.original.clean_coherence_power,
            "private bridge support relaxations did not preserve original paths and admit the planted ramp");
    near(actual.rate_relaxed.clean_coherence_power,1.,3e-14L,"planted wider-rate bridge lost coherent energy");
    require(actual.groups==2&&actual.correlation_products==159*input.plan.blocks&&
            actual.max_plus_terms==(97+291+159)*32*2,"private bridge work lost its full partial-edge accounting");
    std::stop_source stop;stop.request_stop();
    rejects([&]{(void)hf_phase_bridge_support_diagnostic(input.plan,input.q[0],input.energy[0],energy,stop.get_token());},
            "private bridge support ignored cancellation");
    rejects([&]{(void)hf_phase_bridge_support_diagnostic(input.plan,input.q[0],input.energy[0],0.);},
            "private bridge support accepted an undefined clean energy");
}

void bridge_phase_invariant_control() {
    Input input(32*1920);
    for (std::size_t j=0;j<input.plan.blocks;++j) {
        input.energy[0][j]=1.;
        input.q[0][j]=Complex(std::polar(1.L,tau*9*sample_center(j)/64));
    }
    const auto result=hf_phase_bridge_support_diagnostic(input.plan,input.q[0],input.energy[0],input.plan.blocks);
    require(result.phase_relaxed.clean_coherence_power>result.original.clean_coherence_power&&
            result.rate_relaxed.clean_coherence_power>=result.original.clean_coherence_power,
            "quarter-bin constant carrier did not discriminate boundary phase discretization");
    // A quarter-bin tone advances 18 phase bins in four seconds. Holding
    // bin 2 and adding one phase bin advances only 17. This explicit,
    // feasible combination of holds, adjacent ramps and phase steps repeats
    // every four groups after advancing eight phase bins modulo 32.
    constexpr std::array frequency{2,2,2,3,2};
    constexpr std::array phase_offset{1,1,-1,-1};
    long double witness=0;
    int phase=1;
    for (std::size_t group=0;group<8;++group) {
        const auto i=group%4;
        witness+=.5L*bridge_emission(input,0,group,frequency[i],frequency[i+1],tau*phase/32,1.,1.)+30.L;
        phase+=4*(frequency[i]+frequency[i+1])+phase_offset[i];
    }
    near(witness/input.plan.blocks,std::sqrt(.9460444369072087L),2e-13L,
        "quarter-bin analytic witness changed");
    require(result.phase_relaxed.real_correlation+3e-12>=witness,
            "phase relaxation excluded a feasible quarter-bin path");

    // This separate tone is one phase bin per four seconds off bin 2.
    // Holding bin 2 with a +1 phase step bounds the error to one bin.
    long double close_witness=0;
    for (std::size_t j=0;j<input.plan.blocks;++j) {
        const auto t=sample_center(j);
        input.q[0][j]=Complex(std::polar(1.L,tau*17*t/128));
        const auto phase_cycles=17.L*(j/60)/32+(t-4*(j/60))/8;
        const auto difference=tau*(17*t/128-phase_cycles);
        require(std::abs(difference)<=tau/32,"near-bin tone exceeded its analytical phase-jump bound");
        close_witness+=std::cos(difference);
    }
    const auto close=hf_phase_bridge_support_diagnostic(input.plan,input.q[0],input.energy[0],input.plan.blocks);
    require(close.phase_relaxed.real_correlation+3e-12>=close_witness&&
        close.phase_relaxed.clean_coherence_power>close.original.clean_coherence_power,
        "phase relaxation did not retain the independent near-bin path");
}
} // namespace

int main() {
    try {
        two_group_enumeration();
        dynamic_range();
        white_noise_normalization();
        (void)fixture(false);
        (void)fixture(true);
        rejection_and_missing_data();
        exhaustive_clean_trace();
        analytic_clean_trace();
        const HfPreparedPhaseBridge bridge;
        exhaustive_phase_bridge(bridge);
        bridge_noise_and_bounds(bridge);
        bridge_coherence(bridge);
        bridge_support_diagnostic();
        bridge_phase_invariant_control();
        std::cout << "HF phase filter: " << checks << " checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "HF phase filter failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
