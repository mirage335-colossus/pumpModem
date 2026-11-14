#include "appraisal/hf_phase_filter.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace appraisal {
namespace {
using Plan = HfPhaseFilterPlan;
using Complex = datapump::triad::v3::Complex;
constexpr auto phases = Plan::phase_count, frequencies = Plan::frequency_count;
constexpr auto radii = Plan::gain_radii.size();
constexpr int frequency_center = static_cast<int>(frequencies / 2);
constexpr int phase_wrap = static_cast<int>(phases);
constexpr std::size_t states = phases * frequencies;
constexpr double tau = 2 * std::numbers::pi;
constexpr double log_two = std::numbers::ln2, log_quarter = -2 * log_two;
constexpr double log_three_quarters = -.28768207245178092744;
using Prior = HfPhaseFilterPrior;
constexpr HfPhaseFilterTransition walk_transition{.5, .25, .75};
constexpr HfPhaseFilterTransition sticky_transition{31. / 32, 1. / 64, 63. / 64};
constexpr HfPhaseFilterTransition unweighted_path{};
constexpr HfPhaseFilterTransition walk_log_transition{-log_two, log_quarter, log_three_quarters};

using State = std::array<double, states>;
using LagRotations = std::array<std::array<Complex, Plan::blocks_per_group>, frequencies>;
constexpr std::size_t trace_bookkeeping_bytes = 256U * 1024;

struct Rotations {
    LagRotations lag{};
    std::array<Complex, phases> phase{};
    std::array<std::array<std::size_t, phases>, frequencies> previous{};

    Rotations() {
        for (std::size_t f = 0; f < frequencies; ++f) {
            const auto k = static_cast<int>(f) - frequency_center;
            const auto hz = k * Plan::frequency_step_hz;
            for (std::size_t j = 0; j < Plan::blocks_per_group; ++j) {
                const double seconds = (j * Plan::block_rows + (Plan::block_rows - 1) * .5) /
                    Plan::sample_rate;
                lag[f][j] = std::polar(1., -tau * hz * seconds);
            }
            for (std::size_t p = 0; p < phases; ++p) {
                const auto turn = static_cast<int>(p) - 2 * k;
                previous[f][p] = static_cast<std::size_t>((turn + phase_wrap) % phase_wrap);
            }
        }
        for (std::size_t p = 0; p < phases; ++p)
            phase[p] = std::polar(1., -tau * p / phases);
    }
};

void require(bool valid, const char* message) {
    if (!valid) throw std::invalid_argument(message);
}

void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("HF phase filter cancelled");
}

HfPhaseFilterTransition log_transition(Prior prior) {
    switch (prior) {
    // Reuse the original constants exactly for the default and private paths.
    case Prior::walk: return walk_log_transition;
    case Prior::sticky: return {std::log(sticky_transition.stay), -6 * log_two,
                                std::log(sticky_transition.edge_stay)};
    }
    throw std::invalid_argument("unknown HF phase filter transition prior");
}

State phase_projection(std::span<const Complex> correlation, const Rotations& rotations) {
    State projection{};
    for (std::size_t f = 0; f < frequencies; ++f) {
        Complex sum{};
        for (std::size_t j = 0; j < correlation.size(); ++j)
            sum += correlation[j] * rotations.lag[f][j];
        for (std::size_t p = 0; p < phases; ++p)
            projection[f * phases + p] = std::real(sum * rotations.phase[p]);
    }
    return projection;
}

double emit(State& weights, const State& projection, double radius, double energy) {
    auto maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < states; ++i) {
        weights[i] += 2 * radius * projection[i];
        maximum = std::max(maximum, weights[i]);
    }
    require(std::isfinite(maximum), "HF phase filter has invalid emission projection");
    double normalizer = 0;
    for (auto& value : weights) {
        value -= maximum;
        normalizer += std::exp(value);
    }
    const auto log_normalizer = std::log(normalizer);
    for (auto& value : weights) value -= log_normalizer;
    const auto answer = (maximum - radius * radius * energy) + log_normalizer;
    require(std::isfinite(answer), "HF phase filter has invalid emission likelihood");
    return answer;
}

double log_add(double a, double b) {
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}

double log_add_weighted(double a,double b) {
    if (a==-std::numeric_limits<double>::infinity()) return b;
    return log_add(a,b);
}

template<class Combine>
void advance(State& weights, const Rotations& rotations, HfPhaseFilterTransition log_prior, Combine combine) {
    State next{};
    for (std::size_t f = 0; f < frequencies; ++f) {
        // At either boundary the disallowed step stays at the edge.
        const auto stay = f == 0 || f + 1 == frequencies ? log_prior.edge_stay : log_prior.stay;
        const auto neighbor = log_prior.step_each;
        for (std::size_t p = 0; p < phases; ++p) {
            auto weight = weights[f * phases + rotations.previous[f][p]] + stay;
            if (f != 0)
                weight = combine(weight, weights[(f - 1) * phases + rotations.previous[f - 1][p]] + neighbor);
            if (f + 1 != frequencies)
                weight = combine(weight, weights[(f + 1) * phases + rotations.previous[f + 1][p]] + neighbor);
            next[f * phases + p] = weight;
        }
    }
    weights = next;
}

const Rotations& phase_filter_rotations() {
    static const Rotations rotations;
    return rotations;
}

double normalize(std::span<const Complex> q, std::span<const double> energies, double variance,
                 std::span<Complex> normalized) {
    double energy = 0;
    for (std::size_t j = 0; j < q.size(); ++j) {
        const auto g = energies[j];
        require(std::isfinite(g) && g >= 0 && (g != 0 || q[j] == Complex{}),
                "HF phase filter requires finite consistent template energy");
        normalized[j] = q[j] / variance;
        require(std::isfinite(normalized[j].real()) && std::isfinite(normalized[j].imag()),
                "HF phase filter requires finite normalized correlation");
        energy += g / variance;
    }
    require(std::isfinite(energy), "HF phase filter requires finite normalized energy");
    return energy;
}

std::array<double, radii> score_bit(std::span<const Complex> q, std::span<const double> energies,
                                   double variance, const Rotations& rotations,
                                   HfPhaseFilterTransition log_prior, std::stop_token stop) {
    std::array<double, radii> score{};
    std::array<State, radii> weights{};
    for (auto& radius_weights : weights) radius_weights.fill(-std::log(static_cast<double>(states)));
    std::array<Complex, Plan::blocks_per_group> normalized{};
    for (std::size_t first = 0; first < q.size(); first += Plan::blocks_per_group) {
        check_stop(stop);
        const auto count = std::min(Plan::blocks_per_group, q.size() - first);
        const auto energy = normalize(q.subspan(first,count),energies.subspan(first,count),variance,normalized);
        const auto projection = phase_projection(std::span(normalized).first(count), rotations);
        for (std::size_t r = 0; r < radii; ++r) {
            score[r] += emit(weights[r], projection, Plan::gain_radii[r], energy);
            if (first + count < q.size()) advance(weights[r], rotations, log_prior, log_add);
        }
    }
    return score;
}

struct BridgeEdge {
    std::array<Complex,HfPreparedPhaseBridge::blocks_per_group> rotations{};
    std::size_t from=0,to=0,phase_advance=0;
    double log_weight=0;
};

using BridgeEdges=std::array<BridgeEdge,HfPreparedPhaseBridge::edge_count>;

std::size_t bridge_phase_advance(int k,int l) {
    return static_cast<std::size_t>((4*(k+l)+4*phase_wrap)%phase_wrap);
}

template<class Term>
void each_bridge_edge(int maximum_step, Term&& term) {
    for (std::size_t from=0;from<frequencies;++from) {
        const auto k=static_cast<int>(from)-frequency_center;
        const auto lower=std::max(-frequency_center,k-maximum_step);
        const auto upper=std::min(frequency_center,k+maximum_step);
        for (auto l=lower;l<=upper;++l)
            term(from,static_cast<std::size_t>(l+frequency_center),k,l);
    }
}

Complex bridge_rotation(int k,int l,std::size_t block) {
    const double u=(block*Plan::block_rows+(Plan::block_rows-1)*.5)/Plan::sample_rate;
    const double cycles=(k*u+(l-k)*u*u/(2*HfPreparedPhaseBridge::group_seconds))*Plan::frequency_step_hz;
    return std::polar(1.,-tau*cycles);
}

double normalize_bridge_log_weights(State& weights) {
    const auto maximum=*std::max_element(weights.begin(),weights.end());
    require(std::isfinite(maximum),"HF phase bridge has no finite state likelihood");
    double sum=0;
    for (auto& weight:weights) {
        weight-=maximum;
        sum+=std::exp(weight);
    }
    const double log_sum=std::log(sum);
    for (auto& weight:weights) weight-=log_sum;
    return maximum+log_sum;
}

std::array<double,radii> score_bridge_bit(std::span<const Complex> q,std::span<const double> energies,
        double variance,const BridgeEdges& edges,const Rotations& rotations,std::stop_token stop) {
    constexpr auto group_blocks=HfPreparedPhaseBridge::blocks_per_group;
    constexpr auto negative_infinity=-std::numeric_limits<double>::infinity();
    std::array<double,radii> scores{};
    std::array<State,radii> weights{},next{};
    std::array<Complex,group_blocks> normalized{};
    for (auto& row:weights) row.fill(-std::log(static_cast<double>(states)));
    for (std::size_t first=0;first<q.size();first+=group_blocks) {
        check_stop(stop);
        const auto count=std::min(group_blocks,q.size()-first);
        const auto energy=normalize(q.subspan(first,count),energies.subspan(first,count),variance,normalized);
        // The last group always marginalizes the endpoint. This also applies
        // to a partial observation, using the original four-second edge law.
        const bool terminal=first+count==q.size();
        std::array<double,radii> terminal_sum{};
        terminal_sum.fill(negative_infinity);
        for (auto& row:next) row.fill(negative_infinity);
        for (const auto& edge:edges) {
            Complex projection{};
            for (std::size_t j=0;j<count;++j) projection+=normalized[j]*edge.rotations[j];
            for (std::size_t p=0;p<phases;++p) {
                const auto real=std::real(projection*rotations.phase[p]);
                const auto source=edge.from*phases+p;
                const auto dest=edge.to*phases+(p+edge.phase_advance)%phases;
                for (std::size_t r=0;r<radii;++r) {
                    const double score=weights[r][source]+edge.log_weight+2*Plan::gain_radii[r]*real;
                    require(std::isfinite(score),"HF phase bridge emission is nonfinite");
                    auto& value=terminal ? terminal_sum[r] : next[r][dest];
                    value=log_add_weighted(value,score);
                }
            }
        }
        for (std::size_t r=0;r<radii;++r) {
            const double normalization=terminal ? terminal_sum[r] : normalize_bridge_log_weights(next[r]);
            const auto radius=Plan::gain_radii[r];
            scores[r]+=normalization-radius*radius*energy;
        }
        if (!terminal) weights=next;
    }
    return scores;
}

struct TraceTrack {
    State value{};
    std::array<std::uint16_t,states> jumps{};
    std::vector<std::uint8_t> predecessors;

    explicit TraceTrack(std::size_t groups) : predecessors((groups-1)*states) {}

    void step(const Rotations& rotations, const State& projection, std::size_t group,
              double penalty, std::stop_token stop) {
        check_stop(stop);
        for (std::size_t i=0;i<states;++i) value[i]+=projection[i];
        if (group*states==predecessors.size()) return;
        State next{};
        std::array<std::uint16_t,states> next_jumps{};
        for (std::size_t f=0;f<frequencies;++f) {
            const auto begin=f ? f-1 : f;
            const auto end=std::min(f+1,frequencies-1);
            for (std::size_t p=0;p<phases;++p) {
                const auto dest=f*phases+p;
                auto best=-std::numeric_limits<double>::infinity();
                std::uint16_t fewest=std::numeric_limits<std::uint16_t>::max();
                auto predecessor=f;
                for (std::size_t old=begin;old<=end;++old) {
                    const auto source=old*phases+rotations.previous[old][p];
                    const auto changed=old!=f;
                    const auto weight=changed ? value[source]-penalty : value[source];
                    const auto count=static_cast<std::uint16_t>(jumps[source]+changed);
                    // Compare predecessor indices in ascending order. This
                    // keeps the first state on an exact score/jump tie.
                    if (weight>best||(weight==best&&count<fewest)) {
                        best=weight;
                        fewest=count;
                        predecessor=old;
                    }
                }
                next[dest]=best;
                next_jumps[dest]=fewest;
                predecessors[group*states+dest]=static_cast<std::uint8_t>(predecessor+1-f);
            }
        }
        value=next;
        jumps=next_jumps;
    }

    std::size_t best_state() const {
        std::size_t best=0;
        for (std::size_t i=1;i<states;++i)
            if (value[i]>value[best]||(value[i]==value[best]&&jumps[i]<jumps[best])) best=i;
        return best;
    }

    HfPhaseFilterCleanPath backtrace(std::size_t groups, const Rotations& rotations) const {
        HfPhaseFilterCleanPath path;
        path.frequency_bins.resize(groups);
        path.phase_bins.resize(groups);
        std::size_t state=best_state();
        for (std::size_t g=groups;g--;) {
            const auto frequency=state/phases, phase=state%phases;
            path.frequency_bins[g]=static_cast<int>(frequency)-frequency_center;
            path.phase_bins[g]=static_cast<int>(phase);
            if (g) {
                const auto old=frequency+predecessors[(g-1)*states+state]-1;
                state=old*phases+rotations.previous[old][phase];
            }
        }
        return path;
    }
};

double group_duration(const Plan& plan, std::size_t group) {
    const auto rows=std::min(Plan::sample_rate,plan.samples-group*Plan::sample_rate);
    return static_cast<double>(rows)/Plan::sample_rate;
}

double carrier_frequency(const Plan& plan, const HfPhaseFilterCarrierReference& carrier, std::size_t group) {
    return (carrier.group_boundary_cycles[group+1]-carrier.group_boundary_cycles[group])/
        group_duration(plan,group);
}

int direction(double difference, double tolerance = 0.) {
    return static_cast<int>(difference>tolerance)-static_cast<int>(difference < -tolerance);
}

void summarize_carrier(const Plan& plan, const HfPhaseFilterCarrierReference& carrier,
                       HfPhaseFilterCleanTrace& trace) {
    double squared_steps=0;
    int previous=0;
    for (std::size_t g=1;g<plan.groups;++g) {
        const auto delta=carrier_frequency(plan,carrier,g)-carrier_frequency(plan,carrier,g-1);
        trace.carrier_frequency_total_variation_hz+=std::abs(delta);
        squared_steps+=delta*delta;
        // A constant analytic reference can acquire tiny roundoff signs.
        const auto sign=direction(delta,1e-9);
        if (sign&&previous&&sign!=previous) ++trace.carrier_reversal_count;
        if (sign) previous=sign;
    }
    if (plan.groups>1)
        trace.carrier_frequency_rms_step_hz=std::sqrt(squared_steps/(plan.groups-1));
}

void summarize_frequency_path(const Plan& plan, const HfPhaseFilterCarrierReference& carrier,
                              HfPhaseFilterCleanPath& path) {
    const auto sticky=log_transition(Prior::sticky);
    path.walk_log_prior=path.sticky_log_prior=-std::log(static_cast<double>(states));
    path.longest_dwell_groups=1;
    std::size_t dwell=1, four_group_terms=0;
    int previous_delta=0, previous_jump=0;
    double carrier_squared_errors=0, four_group_squared_errors=0, block_error=0;
    for (std::size_t g=0;g<plan.groups;++g) {
        const int f=path.frequency_bins[g];
        const auto dt=group_duration(plan,g);
        const auto physical=carrier_frequency(plan,carrier,g);
        const auto error=f*Plan::frequency_step_hz-physical;
        carrier_squared_errors+=dt*error*error;
        if (dt==1.) {
            block_error+=error;
            if (g%4==3) {
                four_group_squared_errors+=block_error*block_error/16;
                ++four_group_terms;
                block_error=0;
            }
        }
        if (!g) continue;
        const int delta=f-path.frequency_bins[g-1];
        if (delta) {
            ++path.jumps;
            path.up_jumps+=delta>0;
            path.down_jumps+=delta<0;
            if (previous_delta&&delta!=previous_delta) ++path.adjacent_reversals;
            if (previous_jump&&delta!=previous_jump) {
                ++path.successive_jump_reversals;
                const auto physical_step=physical-carrier_frequency(plan,carrier,g-1);
                if (std::abs(physical_step)<.5*Plan::frequency_step_hz)
                    ++path.reversals_below_half_bin_carrier_change;
            }
            previous_jump=delta;
            path.walk_log_prior+=walk_log_transition.step_each;
            path.sticky_log_prior+=sticky.step_each;
            dwell=1;
        } else {
            const bool edge=std::abs(f)==frequency_center;
            path.edge_stays+=edge;
            path.walk_log_prior+=edge ? walk_log_transition.edge_stay : walk_log_transition.stay;
            path.sticky_log_prior+=edge ? sticky.edge_stay : sticky.stay;
            path.longest_dwell_groups=std::max(path.longest_dwell_groups,++dwell);
        }
        previous_delta=delta;
    }
    path.carrier_frequency_rms_hz=std::sqrt(carrier_squared_errors*Plan::sample_rate/plan.samples);
    if (four_group_terms) path.carrier_frequency_4_group_rms_hz=std::sqrt(four_group_squared_errors/four_group_terms);
}

double expected_block_cycles(const HfPhaseFilterCleanPath& path, std::size_t block) {
    const auto g=block/Plan::blocks_per_group;
    const auto j=block%Plan::blocks_per_group;
    const double local_time=(j*Plan::block_rows+(Plan::block_rows-1)*.5)/Plan::sample_rate;
    return static_cast<double>(path.phase_bins[g])/phases+path.frequency_bins[g]*Plan::frequency_step_hz*local_time;
}

struct CircularResidual {
    double weight=0, squared=0;
    Complex mean{};
    void add(double cycles, double w) {
        const auto radians=tau*std::remainder(cycles,1.);
        weight+=w;
        squared+=w*radians*radians;
        mean+=std::polar(w,radians);
    }
    double rms() const { return weight ? std::sqrt(squared/weight) : 0.; }
};

void summarize_phase_path(std::span<const Complex> clean_q, const HfPhaseFilterCarrierReference& carrier,
                          HfPhaseFilterCleanPath& path, std::stop_token stop) {
    CircularResidual effective, analytic;
    for (std::size_t i=0;i<clean_q.size();++i) {
        if (i%Plan::blocks_per_group==0) check_stop(stop);
        const auto cycles=expected_block_cycles(path,i);
        const auto weight=std::abs(clean_q[i]);
        effective.add(std::arg(clean_q[i])/tau-cycles,weight);
        analytic.add(carrier.block_center_cycles[i]-cycles,weight);
    }
    path.real_correlation=effective.mean.real();
    path.effective_phase_rms_radians=effective.rms();
    path.carrier_phase_rms_radians=analytic.rms();
    const double effective_offset=std::arg(effective.mean)/tau, analytic_offset=std::arg(analytic.mean)/tau;
    effective={};analytic={};
    for (std::size_t i=0;i<clean_q.size();++i) {
        if (i%Plan::blocks_per_group==0) check_stop(stop);
        const auto cycles=expected_block_cycles(path,i);
        const auto weight=std::abs(clean_q[i]);
        effective.add(std::arg(clean_q[i])/tau-cycles-effective_offset,weight);
        analytic.add(carrier.block_center_cycles[i]-cycles-analytic_offset,weight);
    }
    path.effective_aligned_phase_rms_radians=effective.rms();
    path.carrier_aligned_phase_rms_radians=analytic.rms();
}
} // namespace

struct HfPreparedPhaseBridge::Impl {
    BridgeEdges edges{};
};

HfPreparedPhaseBridge::HfPreparedPhaseBridge(std::stop_token stop) {
    static_assert(sizeof(Impl)+1024<=shared_workspace_bytes);
    check_stop(stop);
    auto impl=std::make_unique<Impl>();
    std::size_t index=0;
    each_bridge_edge(1,[&](std::size_t from,std::size_t to,int k,int l) {
        check_stop(stop);
        auto& edge=impl->edges[index++];
        edge.from=from;
        edge.to=to;
        edge.phase_advance=bridge_phase_advance(k,l);
        const bool boundary=from==0||from+1==frequencies;
        edge.log_weight=from==to ?
            (boundary ? walk_log_transition.edge_stay : walk_log_transition.stay) :
            walk_log_transition.step_each;
        for (std::size_t j=0;j<blocks_per_group;++j) edge.rotations[j]=bridge_rotation(k,l,j);
    });
    require(index==edge_count,"HF phase bridge edge geometry changed");
    impl_=std::move(impl);
}

HfPreparedPhaseBridge::~HfPreparedPhaseBridge()=default;

HfPhaseFilterReport HfPreparedPhaseBridge::score(const Plan& plan,const HfPhaseBlockCorrelations& q,
        const HfPhaseBlockEnergies& energies,double variance,std::stop_token stop) const {
    check_stop(stop);
    require(plan==hf_phase_filter_plan(plan.samples),"HF phase bridge plan is inconsistent");
    require(std::isfinite(variance)&&variance>0,"HF phase bridge requires finite positive noise variance");
    const auto& rotations=phase_filter_rotations();
    HfPhaseFilterReport report;
    for (std::size_t bit=0;bit<2;++bit) {
        require(q[bit].size()==plan.blocks&&energies[bit].size()==plan.blocks,
                "HF phase bridge block statistics do not match the public aperture");
        auto& scores=report.radius_log_e[bit];
        scores=score_bridge_bit(q[bit],energies[bit],variance,impl_->edges,rotations,stop);
        require(std::isfinite(scores[0])&&std::isfinite(scores[1]),"HF phase bridge evidence is nonfinite");
        report.bit_log_e[bit]=log_add(scores[0],scores[1])-log_two;
    }
    return report;
}

HfPhaseBridgeSupportDiagnostic hf_phase_bridge_support_diagnostic(const Plan& plan,
        std::span<const Complex> clean_q,std::span<const double> energies,double clean_energy,std::stop_token stop) {
    check_stop(stop);
    require(plan==hf_phase_filter_plan(plan.samples)&&clean_q.size()==plan.blocks&&energies.size()==plan.blocks,
            "private HF bridge support requires complete block geometry");
    require(std::isfinite(clean_energy)&&clean_energy>0.,"private HF bridge support needs positive finite clean energy");
    using Diagnostic=HfPhaseBridgeSupportDiagnostic;
    constexpr auto group_blocks=HfPreparedPhaseBridge::blocks_per_group;
    constexpr auto negative_infinity=-std::numeric_limits<double>::infinity();
    static_assert(Diagnostic::original_edges==HfPreparedPhaseBridge::edge_count);
    static_assert(Diagnostic::phase_relaxed_edges==3*Diagnostic::original_edges);
    static_assert(Diagnostic::rate_relaxed_edges==5*frequencies-6);
    struct EdgeProjection {
        BridgeEdge edge;
        bool adjacent=false;
    };
    using Edges=std::array<EdgeProjection,Diagnostic::rate_relaxed_edges>;
    static_assert(sizeof(Edges)+6*sizeof(State)+16*1024<=Diagnostic::workspace_reserved_bytes);
    const auto edges=std::make_unique<Edges>();
    std::size_t edge_index=0;
    each_bridge_edge(2,[&](std::size_t from,std::size_t to,int k,int l) {
        check_stop(stop);
        auto& entry=(*edges)[edge_index++];
        auto& edge=entry.edge;
        edge.from=from;
        edge.to=to;
        edge.phase_advance=bridge_phase_advance(k,l);
        entry.adjacent=std::abs(k-l)<=1;
        for (std::size_t j=0;j<group_blocks;++j) edge.rotations[j]=bridge_rotation(k,l,j);
    });
    require(edge_index==edges->size(),"private HF bridge support edge geometry changed");
    const auto& rotations=phase_filter_rotations();
    // Each variant consumes the same cached 159 correlations once per group.
    // Variant order is original, phase relaxation, and rate relaxation.
    std::array<State,3> scores{},next{};
    std::array<Complex,group_blocks> normalized{};
    std::array<Complex,Diagnostic::rate_relaxed_edges> correlations{};
    double template_energy=0.;
    for (std::size_t first=0;first<plan.blocks;first+=group_blocks) {
        check_stop(stop);
        const auto count=std::min(group_blocks,plan.blocks-first);
        template_energy+=normalize(clean_q.subspan(first,count),energies.subspan(first,count),1.,normalized);
        for (std::size_t e=0;e<edges->size();++e) {
            Complex q{};
            for (std::size_t j=0;j<count;++j) q+=normalized[j]*(*edges)[e].edge.rotations[j];
            require(std::isfinite(q.real())&&std::isfinite(q.imag()),"private HF bridge clean correlation is nonfinite");
            correlations[e]=q;
        }
        for (auto& values:next) values.fill(negative_infinity);
        for (std::size_t e=0;e<edges->size();++e) {
            const auto& entry=(*edges)[e];
            const auto& edge=entry.edge;
            for (std::size_t phase=0;phase<phases;++phase) {
                const auto contribution=std::real(correlations[e]*rotations.phase[phase]);
                const auto source=edge.from*phases+phase;
                const auto destination_phase=(phase+edge.phase_advance)%phases;
                const auto destination=edge.to*phases+destination_phase;
                auto& rate=next[2][destination];
                rate=std::max(rate,scores[2][source]+contribution);
                if (!entry.adjacent) continue;
                auto& original=next[0][destination];
                original=std::max(original,scores[0][source]+contribution);
                const auto value=scores[1][source]+contribution;
                for (std::size_t delta : {phases-1,std::size_t{0},std::size_t{1}}) {
                    auto& phase_relaxed=next[1][edge.to*phases+(destination_phase+delta)%phases];
                    phase_relaxed=std::max(phase_relaxed,value);
                }
            }
        }
        scores=next;
    }
    const double denominator=std::sqrt(clean_energy)*std::sqrt(template_energy);
    require(std::isfinite(denominator)&&denominator>0.,"private HF bridge support normalization is invalid");
    const auto summarize=[&](const State& values) {
        const auto peak=*std::max_element(values.begin(),values.end());
        const auto fraction=peak/denominator;
        require(std::isfinite(fraction)&&fraction>=0.&&fraction<=1.+1e-8,
                "private HF bridge support exceeds the clean Cauchy bound");
        return HfPhaseBridgeSupportProjection{peak,std::min(1.,fraction*fraction)};
    };
    Diagnostic result;
    result.groups=1+(plan.blocks-1)/group_blocks;
    result.correlation_products=Diagnostic::rate_relaxed_edges*plan.blocks;
    result.max_plus_terms=(Diagnostic::original_edges+Diagnostic::phase_relaxed_edges+Diagnostic::rate_relaxed_edges)*
        phases*result.groups;
    result.original=summarize(scores[0]);
    result.phase_relaxed=summarize(scores[1]);
    result.rate_relaxed=summarize(scores[2]);
    return result;
}

HfPhaseFilterTransition hf_phase_filter_transition(Prior prior) {
    switch (prior) {
    case Prior::walk: return walk_transition;
    case Prior::sticky: return sticky_transition;
    }
    throw std::invalid_argument("unknown HF phase filter transition prior");
}

HfPhaseFilterPlan hf_phase_filter_plan(std::size_t samples) {
    require(samples > 0 && samples <= Plan::maximum_samples, "HF phase filter aperture is outside the 600 s limit");
    const auto blocks = 1 + (samples - 1) / Plan::block_rows;
    const auto groups = 1 + (blocks - 1) / Plan::blocks_per_group;
    constexpr auto bits = 2U;
    return {samples, blocks, groups, bits * frequencies * blocks, bits * radii * states * groups,
            bits * radii * phases * (3 * frequencies - 2) * (groups - 1)};
}

std::size_t hf_phase_filter_clean_trace_workspace(const Plan& plan) {
    require(plan==hf_phase_filter_plan(plan.samples),"HF phase filter trace plan is inconsistent");
    return 2*states*(plan.groups-1)+trace_bookkeeping_bytes;
}

HfPhaseFilterReport hf_phase_filter(const Plan& plan, const HfPhaseBlockCorrelations& q,
    const HfPhaseBlockEnergies& energies, double variance, std::stop_token stop, Prior prior) {
    check_stop(stop);
    require(plan == hf_phase_filter_plan(plan.samples), "HF phase filter plan is inconsistent");
    require(std::isfinite(variance) && variance > 0, "HF phase filter requires finite positive noise variance");
    const auto& rotations = phase_filter_rotations();
    const auto log_prior = log_transition(prior);
    HfPhaseFilterReport report;
    for (std::size_t bit = 0; bit < 2; ++bit) {
        require(q[bit].size() == plan.blocks && energies[bit].size() == plan.blocks,
                "HF phase filter block statistics do not match the public aperture");
        const auto scores = score_bit(q[bit], energies[bit], variance, rotations, log_prior, stop);
        const auto maximum = std::max(scores[0], scores[1]);
        const auto minimum = std::min(scores[0], scores[1]);
        require(std::isfinite(maximum) && std::isfinite(minimum), "HF phase filter accumulated likelihood is nonfinite");
        report.radius_log_e[bit] = scores;
        report.bit_log_e[bit] = log_add(maximum, minimum) - log_two;
    }
    return report;
}

HfPhaseFilterPathDiagnostic hf_phase_filter_path_diagnostic(const Plan& plan, std::span<const Complex> clean_q,
    std::span<const Complex> noisy_q, std::span<const double> energies, double clean_energy,
    double variance, std::stop_token stop, std::optional<HfPhaseFilterCarrierReference> carrier) {
    check_stop(stop);
    require(plan == hf_phase_filter_plan(plan.samples) && clean_q.size() == plan.blocks &&
        noisy_q.size() == plan.blocks && energies.size() == plan.blocks,
        "HF phase path diagnostic requires complete consistent block statistics");
    require(std::isfinite(clean_energy) && clean_energy > 0 && std::isfinite(variance) && variance > 0,
        "HF phase path diagnostic requires finite positive clean energy and noise variance");
    const auto& rotations = phase_filter_rotations();
    const auto maximum = [](double a,double b) { return std::max(a,b); };
    const auto state_maximum = [](const State& values) { return *std::max_element(values.begin(),values.end()); };
    State clean_path{};
    std::array<State,radii> raw_path{}, prior_path{}, posterior{};
    const auto initial_log_prior = -std::log(static_cast<double>(states));
    for (auto& path : prior_path) path.fill(initial_log_prior);
    for (auto& path : posterior) path.fill(initial_log_prior);
    std::array<Complex,Plan::blocks_per_group> normalized{};
    HfPhaseFilterPathDiagnostic report;
    std::optional<TraceTrack> maximum_trace, penalized_trace;
    double trace_normalizer=0;
    if (carrier) {
        require(carrier->block_center_cycles.size()==plan.blocks&&carrier->group_boundary_cycles.size()==plan.groups+1,
                "HF phase filter trace carrier reference extent changed");
        double total=0;
        for (const auto value:energies) {
            require(std::isfinite(value)&&value>=0,"HF phase filter trace needs finite template energies");
            total+=value;
        }
        require(total>0&&std::isfinite(total),"HF phase filter trace needs nonzero finite template energy");
        for (const auto series:{carrier->block_center_cycles,carrier->group_boundary_cycles})
            for (const auto value:series)
                require(std::isfinite(value),"HF phase filter trace carrier reference is nonfinite");
        trace_normalizer=std::sqrt(total)*std::sqrt(clean_energy);
        require(trace_normalizer>0&&std::isfinite(trace_normalizer),"HF phase filter trace normalization is nonfinite");
        auto& trace=report.clean_trace.emplace();
        trace.workspace_reserved_bytes=hf_phase_filter_clean_trace_workspace(plan);
        trace.predecessor_labels=2*states*(plan.groups-1);
        trace.analytic_carrier_points=plan.blocks+plan.groups+1;
        trace.extra_state_evaluations=states*plan.groups;
        trace.extra_transition_edges=phases*(3*frequencies-2)*(plan.groups-1);
        trace.penalty_correlation_units=trace_normalizer/(64*plan.groups);
        trace.certificate_tolerance_correlation=
            (128*std::numeric_limits<double>::epsilon()*(plan.groups+1))*trace_normalizer;
        maximum_trace.emplace(plan.groups);
        penalized_trace.emplace(plan.groups);
        require(maximum_trace->predecessors.capacity()+penalized_trace->predecessors.capacity()<=trace.predecessor_labels,
                "HF phase filter trace exceeded its predecessor reservation");
    }
    double template_energy = 0;
    for (std::size_t first = 0; first < plan.blocks; first += Plan::blocks_per_group) {
        check_stop(stop);
        const auto count = std::min(Plan::blocks_per_group, plan.blocks - first);
        const auto group_energies = energies.subspan(first,count);
        template_energy += normalize(clean_q.subspan(first,count),group_energies,1.,normalized);
        const auto clean = phase_projection(std::span(normalized).first(count), rotations);
        const auto noise_scaled_energy = normalize(noisy_q.subspan(first,count),group_energies,variance,normalized);
        const auto noisy = phase_projection(std::span(normalized).first(count), rotations);
        if (maximum_trace) {
            const auto group=first/Plan::blocks_per_group;
            maximum_trace->step(rotations,clean,group,0.,stop);
            penalized_trace->step(rotations,clean,group,report.clean_trace->penalty_correlation_units,stop);
        } else {
            for (std::size_t i=0;i<states;++i) clean_path[i]+=clean[i];
        }
        for (std::size_t r = 0; r < radii; ++r) {
            const double radius = Plan::gain_radii[r];
            for (std::size_t i = 0; i < states; ++i) {
                const auto log_lr = radius * (2 * noisy[i] - radius * noise_scaled_energy);
                raw_path[r][i] += log_lr;
                prior_path[r][i] += log_lr;
            }
            report.conditional_noisy_log_e[r] += emit(posterior[r],noisy,radius,noise_scaled_energy);
        }
        if (first + count < plan.blocks) {
            if (!maximum_trace) advance(clean_path,rotations,unweighted_path,maximum);
            for (std::size_t r = 0; r < radii; ++r) {
                advance(raw_path[r],rotations,unweighted_path,maximum);
                advance(prior_path[r],rotations,walk_log_transition,maximum);
                advance(posterior[r],rotations,walk_log_transition,log_add);
            }
        }
    }
    for (std::size_t r = 0; r < radii; ++r) {
        report.maximum_noisy_log_lr[r] = state_maximum(raw_path[r]);
        report.maximum_prior_weighted_noisy_path_log_e[r] = state_maximum(prior_path[r]);
        require(std::isfinite(report.maximum_noisy_log_lr[r]) &&
                std::isfinite(report.maximum_prior_weighted_noisy_path_log_e[r]) &&
                std::isfinite(report.conditional_noisy_log_e[r]), "HF phase path diagnostic score is nonfinite");
    }
    report.optimal_clean_correlation_real = maximum_trace ?
        maximum_trace->value[maximum_trace->best_state()] : state_maximum(clean_path);
    require(std::isfinite(template_energy) && template_energy > 0 &&
            std::isfinite(report.optimal_clean_correlation_real) && report.optimal_clean_correlation_real >= 0,
            "HF phase path diagnostic has invalid clean projection");
    const auto fraction = (report.optimal_clean_correlation_real / std::sqrt(template_energy)) / std::sqrt(clean_energy);
    require(fraction <= 1 + 1e-8, "HF phase path diagnostic projection exceeds supplied clean energy");
    report.clean_coherence_power_lower_bound = std::min(1.,fraction * fraction);
    if (maximum_trace) {
        auto& trace=*report.clean_trace;
        trace.maximum_projection=maximum_trace->backtrace(plan.groups,rotations);
        trace.penalized_projection=penalized_trace->backtrace(plan.groups,rotations);
        trace.penalized_objective_correlation=penalized_trace->value[penalized_trace->best_state()];
        summarize_carrier(plan,*carrier,trace);
        const auto tolerance=trace.certificate_tolerance_correlation;
        const auto penalty=trace.penalty_correlation_units;
        require(penalty>0&&std::isfinite(trace.penalized_objective_correlation),
                "HF phase filter trace has a nonfinite objective or zero penalty");
        for (auto* path:{&trace.maximum_projection,&trace.penalized_projection}) {
            summarize_frequency_path(plan,*carrier,*path);
            summarize_phase_path(clean_q,*carrier,*path,stop);
            const auto normalized_projection=path->real_correlation/trace_normalizer;
            path->clean_coherence_power=std::min(1.,normalized_projection*normalized_projection);
        }
        require(std::abs(trace.maximum_projection.real_correlation-report.optimal_clean_correlation_real)<=tolerance&&
            std::abs(trace.penalized_projection.real_correlation-penalty*trace.penalized_projection.jumps-
                trace.penalized_objective_correlation)<=tolerance,
            "HF phase filter traceback does not reproduce the clean objectives");
        trace.target_real_correlation=std::sqrt(HfPhaseFilterCleanTrace::target_projection_power_fraction_of_best_real)*
            report.optimal_clean_correlation_real;
        const auto bound=std::max(0.,std::ceil((trace.target_real_correlation-
            trace.penalized_objective_correlation-tolerance)/penalty));
        require(bound<=trace.maximum_projection.jumps,"HF phase filter trace jump certificate exceeds its witness");
        trace.minimum_jumps_lower_bound=static_cast<std::size_t>(bound);
        trace.target_reached=trace.penalized_projection.real_correlation-tolerance>=trace.target_real_correlation;
    }
    constexpr auto tracks = 1 + 3 * radii;
    report.correlation_products = 2 * frequencies * plan.blocks;
    report.emission_states = tracks * states * plan.groups;
    report.transition_edges = tracks * phases * (3 * frequencies - 2) * (plan.groups - 1);
    return report;
}

} // namespace appraisal
