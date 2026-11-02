#include "datapump/triad_statistics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datapump::triad::statistics {
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool finite_positive(double value) { return std::isfinite(value) && value > 0; }

std::uint64_t saturating_multiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return std::numeric_limits<std::uint64_t>::max();
    return left * right;
}

std::uint64_t ceil_to_u64(long double value, const char* error) {
    require(std::isfinite(value) && value >= 0 &&
            value < static_cast<long double>(std::numeric_limits<std::uint64_t>::max()), error);
    return static_cast<std::uint64_t>(std::ceil(value));
}

long double snap_near_integer(long double value) {
    const auto nearest = std::round(value);
    const auto tolerance = std::min(1e-8L, 16.L * std::numeric_limits<double>::epsilon() *
                                             std::max(1.L, std::abs(value)));
    return std::abs(value - nearest) <= tolerance ? nearest : value;
}

bool dwell_multiple(double value, double dwell) {
    const auto units = static_cast<long double>(value) / dwell;
    return std::isfinite(units) && snap_near_integer(units) == std::round(units);
}

double gamma_upper_bound(std::uint64_t rank, double log_tail_reciprocal) {
    // Laurent–Massart bound: P[X >= r + sqrt(2 r x) + x] <= exp(-x),
    // for X ~ chi-square(2r)/2 == Gamma(r,1). It is valid for all integral
    // ranks and avoids fragile tail-CDF approximations at tiny probabilities.
    require(rank > 0 && std::isfinite(log_tail_reciprocal) && log_tail_reciprocal > 0,
            "invalid Gamma tail parameters");
    const long double r = static_cast<long double>(rank);
    const long double x = static_cast<long double>(log_tail_reciprocal);
    const long double bound = r + std::sqrt(2 * r * x) + x;
    require(std::isfinite(bound) && bound <= std::numeric_limits<double>::max(),
            "Gamma threshold exceeds numeric range");
    const auto rounded = std::nextafter(static_cast<double>(bound),
                                        std::numeric_limits<double>::infinity());
    require(std::isfinite(rounded), "Gamma threshold exceeds numeric range");
    return rounded;
}

long double total_null_log_tests(const PlannerInput& input,
                                 std::uint64_t& attempts,
                                 std::uint64_t& per_attempt) {
    const auto& s = input.search;
    attempts = ceil_to_u64(static_cast<long double>(s.monitoring_horizon_seconds) /
                               s.attempt_spacing_seconds,
                           "attempt count exceeds supported range");
    per_attempt = 1;
    long double log_count = std::log(static_cast<long double>(attempts));
    for (const auto factor : {s.timing_hypotheses, s.epoch_hypotheses,
                              s.sample_clock_hypotheses, s.frequency_hypotheses,
                              s.channel_hypotheses, s.phase_hypotheses,
                              s.bit_hypotheses, s.duration_alternatives}) {
        require(factor > 0, "search-space factors must be nonzero");
        per_attempt = saturating_multiply(per_attempt, factor);
        log_count += std::log(static_cast<long double>(factor));
    }
    require(attempts > 0 && per_attempt > 0, "search schedule is empty");
    return log_count;
}

std::uint64_t rank_for_duration(const PlannerInput& input, double duration) {
    long double blocks = 1;
    if (input.coherence_block_seconds > 0)
        blocks = std::ceil(static_cast<long double>(duration) /
                           input.coherence_block_seconds);
    const auto count = ceil_to_u64(blocks, "block count exceeds supported range");
    const auto rank = saturating_multiply(count, input.rank_per_block);
    require(rank > 0 && rank < std::numeric_limits<std::uint64_t>::max(),
            "rank exceeds supported range");
    return rank;
}

void validate(const PlannerInput& p) {
    const auto& s = p.search;
    require(finite_positive(s.monitoring_horizon_seconds) &&
            finite_positive(s.attempt_spacing_seconds), "invalid monitoring schedule");
    for (const auto factor : {s.timing_hypotheses, s.epoch_hypotheses,
                              s.sample_clock_hypotheses, s.frequency_hypotheses,
                              s.channel_hypotheses, s.phase_hypotheses,
                              s.bit_hypotheses, s.duration_alternatives,
                              s.wrong_message_competitors})
        require(factor > 0, "search-space factors must be nonzero");
    require(std::isfinite(p.cn0_db_hz) && std::isfinite(p.target_correct_probability) &&
            p.target_correct_probability > 0 && p.target_correct_probability < 1,
            "invalid C/N0 or correct-reception target");
    require(std::isfinite(p.false_admission_probability_per_horizon) &&
            p.false_admission_probability_per_horizon > 0 &&
            p.false_admission_probability_per_horizon < 1,
            "false-admission target must lie strictly between zero and one");
    require(finite_positive(p.maximum_duration_seconds) && finite_positive(p.dwell_seconds) &&
            p.minimum_dwells >= 20 && std::abs(p.dwell_seconds - .4) < 1e-12 &&
            finite_positive(p.inner_rate_hz) &&
            p.added_symbols > 0 && p.minimum_inner_coefficients >= 64 &&
            finite_positive(p.baseband_samples_per_second) && p.rank_per_block > 0,
            "invalid waveform or receiver geometry");
    const auto quantum = p.duration_quantum_seconds == 0 ? p.dwell_seconds : p.duration_quantum_seconds;
    require(std::isfinite(p.duration_quantum_seconds) && p.duration_quantum_seconds >= 0 &&
            finite_positive(quantum) && dwell_multiple(quantum, p.dwell_seconds) &&
            quantum >= p.dwell_seconds && std::isfinite(p.duration_offset_seconds) &&
            p.duration_offset_seconds <= 0 && p.duration_offset_seconds > -quantum &&
            dwell_multiple(p.duration_offset_seconds, p.dwell_seconds),
            "duration quantum/offset must define a positive whole-dwell calendar");
    require(std::isfinite(p.coherence_block_seconds) && p.coherence_block_seconds >= 0 &&
            std::isfinite(p.usable_time_fraction) && p.usable_time_fraction > 0 &&
            p.usable_time_fraction <= 1 && std::isfinite(p.maximum_outage_fraction) &&
            p.maximum_outage_fraction >= 0 && p.maximum_outage_fraction < 1 &&
            std::isfinite(p.startup_overhead_seconds) && p.startup_overhead_seconds >= 0 &&
            std::isfinite(p.retained_energy_fraction) && p.retained_energy_fraction > 0 &&
            p.retained_energy_fraction <= 1 && std::isfinite(p.wrong_projection_fraction) &&
            p.wrong_projection_fraction >= 0 && p.wrong_projection_fraction < 1,
            "invalid observation or model bounds");
    require(p.candidate_batch_size > 0 && p.sample_tile_size > 0 &&
            p.operations_per_sample_per_hypothesis > 0 &&
            p.bytes_per_complex_sample > 0, "invalid workspace tile geometry");
}

std::uint64_t estimated_workspace(const PlannerInput& p) {
    auto value = saturating_multiply(p.candidate_batch_size, p.sample_tile_size);
    value = saturating_multiply(value, p.bytes_per_complex_sample);
    value = saturating_multiply(value, 3); // I/Q tiles for three carriers.
    if (value > std::numeric_limits<std::uint64_t>::max() - p.fixed_workspace_bytes)
        return std::numeric_limits<std::uint64_t>::max();
    return value + p.fixed_workspace_bytes;
}

std::uint64_t estimated_operations(const PlannerInput& p, double duration,
                                   std::uint64_t hypotheses) {
    const long double samples = std::ceil(static_cast<long double>(duration) *
                                          p.baseband_samples_per_second);
    if (!std::isfinite(samples) || samples >= std::numeric_limits<std::uint64_t>::max())
        return std::numeric_limits<std::uint64_t>::max();
    auto value = saturating_multiply(static_cast<std::uint64_t>(samples), hypotheses);
    return saturating_multiply(value, p.operations_per_sample_per_hypothesis);
}

double log_of_twice_probability(double probability) {
    return std::log(2.0) - std::log(probability);
}

} // namespace

double gamma_log_survival(std::uint64_t rank,double score) {
    require(rank>0&&std::isfinite(score)&&score>=0,"invalid Gamma survival input");
    if(score==0)return 0;
    if(rank==1)return -score;
    require(rank<=100'000'000,"Gamma rank exceeds bounded numerical solver");
    const long double a=rank,x=score;
    const auto log_factor=a*std::log(x)-x-std::lgamma(a);
    constexpr long double tiny=std::numeric_limits<long double>::min()*1000, epsilon=1e-16L;
    if(x<a+1) {
        long double term=1/a,sum=term;
        for(unsigned i=1;i<=20000;++i){term*=x/(a+i);sum+=term;
            if(std::abs(term)<=std::abs(sum)*epsilon){
                const auto p=std::exp(log_factor)*sum;
                require(p>=0&&p<1,"Gamma lower-tail cancellation");
                return static_cast<double>(std::log1p(-p));
            }
        }
    } else {
        long double b=x+1-a,c=1/tiny,d=1/b,h=d;
        for(unsigned i=1;i<=20000;++i){const long double an=-static_cast<long double>(i)*(i-a);
            b+=2;d=an*d+b;if(std::abs(d)<tiny)d=tiny;
            c=b+an/c;if(std::abs(c)<tiny)c=tiny;
            d=1/d;const auto delta=d*c;h*=delta;
            if(std::abs(delta-1)<=epsilon){require(h>0&&std::isfinite(h),"Gamma continued-fraction failure");
                return static_cast<double>(std::min(0.L,log_factor+std::log(h)));}
        }
    }
    throw std::invalid_argument("Gamma survival iteration bound exhausted");
}

Threshold conservative_null_threshold(std::uint64_t rank, double log_hypotheses,
                                      double family_false_probability) {
    require(std::isfinite(log_hypotheses) && log_hypotheses >= 0,
            "invalid log hypothesis count");
    require(std::isfinite(family_false_probability) && family_false_probability > 0 &&
            family_false_probability < 1, "invalid family false probability");
    const auto log_tail = log_hypotheses - std::log(family_false_probability);
    return {gamma_upper_bound(rank, log_tail), log_hypotheses, -log_tail};
}

Threshold gamma_null_threshold(std::uint64_t rank,double log_hypotheses,double alpha) {
    auto result=conservative_null_threshold(rank,log_hypotheses,alpha);
    if(rank==1){result.score=std::nextafter(-result.log_false_probability_per_hypothesis,
        std::numeric_limits<double>::infinity());result.conservative=false;return result;}
    try {
        const auto target=result.log_false_probability_per_hypothesis;
        double lo=0,hi=result.score;
        if(gamma_log_survival(rank,hi)>target)return result;
        for(unsigned i=0;i<80;++i){const auto mid=lo+(hi-lo)/2;
            if(mid==lo||mid==hi)break;
            if(gamma_log_survival(rank,mid)>target)lo=mid;else hi=mid;
        }
        // Small upward margin for libm/cancellation error; empirical parity is
        // still required. This is a numerical quantile, not interval arithmetic.
        hi=std::nextafter(hi+1e-10*std::max(1.,hi),std::numeric_limits<double>::infinity());
        if(gamma_log_survival(rank,hi)<=target){result.score=hi;result.conservative=false;}
    } catch(const std::invalid_argument&) { /* Explicit conservative fallback. */ }
    return result;
}

double known_channel_binary_error(double squared_distance, double complex_noise_variance) {
    require(std::isfinite(squared_distance) && squared_distance >= 0 &&
            finite_positive(complex_noise_variance), "invalid known-channel distance or noise");
    return 0.5 * std::erfc(.5 * std::sqrt(squared_distance) /
                         std::sqrt(complex_noise_variance));
}

double zero_event_upper_bound(std::uint64_t independent_campaigns, double confidence) {
    require(independent_campaigns > 0 && std::isfinite(confidence) &&
            confidence > 0 && confidence < 1, "invalid binomial confidence input");
    // Exact one-sided Clopper–Pearson bound when zero events were observed.
    return -std::expm1(std::log1p(-confidence) /
                       static_cast<double>(independent_campaigns));
}

DurationPlan plan_duration(const PlannerInput& p) {
    validate(p);
    DurationPlan result;
    std::uint64_t attempts = 0, per_attempt = 0;
    const auto log_tests = total_null_log_tests(p, attempts, per_attempt);
    const auto saturated_tests = saturating_multiply(attempts, per_attempt);
    result.attempts_per_horizon = attempts;
    result.total_search_hypotheses = saturated_tests;
    result.hypothesis_count_saturated = saturated_tests == UINT64_MAX;
    result.log_hypotheses_per_attempt = static_cast<double>(log_tests -
        std::log(static_cast<long double>(attempts)));
    result.log_null_tests_per_horizon = static_cast<double>(log_tests);
    const auto log_alpha = -std::log(p.false_admission_probability_per_horizon);
    const auto x = static_cast<double>(log_tests) + log_alpha;
    result.required_null_log_tail = x;

    // Split the requested correct-reception failure budget between the lower
    // tail of the true statistic and all wrong-message competitors.
    const auto beta = 1 - p.target_correct_probability;
    const auto y = log_of_twice_probability(beta);
    const auto z = std::max(result.log_hypotheses_per_attempt,
                   std::log(static_cast<double>(p.search.wrong_message_competitors))) +
                   log_of_twice_probability(beta);
    result.required_correct_log_tail = y;
    result.required_wrong_log_tail = z;
    result.estimated_workspace_bytes = estimated_workspace(p);
    result.workspace_count_saturated = result.estimated_workspace_bytes == UINT64_MAX;

    if (result.workspace_count_saturated ||
        result.estimated_workspace_bytes > p.maximum_workspace_bytes) {
        result.status = PlannerStatus::computation_infeasible;
        result.status_name = planner_status_name(result.status);
        return result;
    }

    const auto minimum_by_inner = static_cast<double>(p.minimum_inner_coefficients) /
                                  (p.inner_rate_hz / 6);
    const auto useful_fraction = p.usable_time_fraction * (1 - p.maximum_outage_fraction);
    const auto minimum_duration = std::max({8.0,
        static_cast<double>(p.minimum_dwells) * p.dwell_seconds,
        (minimum_by_inner + p.startup_overhead_seconds) / useful_fraction});
    if (minimum_duration > p.maximum_duration_seconds) {
        result.status = PlannerStatus::no_feasible_duration;
        result.status_name = planner_status_name(result.status);
        return result;
    }

    const auto cn0 = std::pow(10.0, p.cn0_db_hz / 10.0);
    require(std::isfinite(cn0) && cn0 > 0, "C/N0 is outside supported numeric range");
    const long double quantum = p.duration_quantum_seconds == 0 ? p.dwell_seconds : p.duration_quantum_seconds;
    const long double offset = p.duration_offset_seconds;
    // Snap only floating-point roundoff near integral coordinates. All actual
    // energy/rank/resource calculations below use the selected elapsed time.
    const auto first_step = std::max<std::uint64_t>(1, ceil_to_u64(snap_near_integer(
        (static_cast<long double>(minimum_duration) - offset) / quantum), "duration coordinate overflow"));
    const auto last_coordinate = snap_near_integer(
        (static_cast<long double>(p.maximum_duration_seconds) - offset) / quantum);
    const auto max_step = ceil_to_u64(std::floor(last_coordinate), "duration coordinate overflow");
    const auto total_null_log = static_cast<double>(log_tests);
    if (max_step < first_step) {
        result.status = PlannerStatus::no_feasible_duration;
        result.status_name = planner_status_name(result.status);
        return result;
    }
    // Charge the bounded work budget to candidates actually evaluated. A long
    // allowed horizon must not reject an otherwise cheap early solution. This
    // remains an exhaustive ordered search; rank changes are not assumed to
    // make the separation margin monotone.
    std::uint64_t evaluated = 0;
    bool resource_exceeded = false;
    for (std::uint64_t step = first_step; step <= max_step;
         ++step) {
        require(evaluated < 2'000'001,
                "duration search exhausted bounded planner grid; unvisited durations remain");
        ++evaluated;
        const auto duration_wide = static_cast<long double>(step) * quantum + offset;
        require(std::isfinite(duration_wide) && duration_wide > 0 &&
                duration_wide <= std::numeric_limits<double>::max(), "duration exceeds numeric range");
        const auto duration = static_cast<double>(duration_wide);
        if (duration > p.maximum_duration_seconds + 1e-10) break;
        const auto usable = std::max(0.0,
            duration * p.usable_time_fraction * (1 - p.maximum_outage_fraction) -
            p.startup_overhead_seconds);
        const auto rank = rank_for_duration(p, duration);
        const auto energy_wide = static_cast<long double>(cn0) * usable * p.retained_energy_fraction;
        require(std::isfinite(energy_wide) && energy_wide <= std::numeric_limits<double>::max(),
                "signal noncentrality exceeds supported numeric range");
        const auto energy = static_cast<double>(energy_wide);
        const auto null_upper = gamma_upper_bound(rank, total_null_log + log_alpha);
        const long double r = static_cast<long double>(rank);
        const long double e = static_cast<long double>(energy);
        const long double eta = static_cast<long double>(p.wrong_projection_fraction);
        const long double true_lower = r + e - std::sqrt(2 * (r + 2 * e) * y);
        const long double wrong_upper = r + eta * e +
            std::sqrt(2 * (r + 2 * eta * e) * z) + z;
        const long double margin = true_lower - std::max<long double>(null_upper, wrong_upper);
        result.duration_seconds = duration;
        result.useful_energy_seconds = usable;
        result.signal_noncentrality = energy;
        result.guaranteed_true_score_lower = static_cast<double>(true_lower);
        result.null_score_upper = null_upper;
        result.wrong_score_upper = static_cast<double>(wrong_upper);
        result.separation_margin = static_cast<double>(margin);
        result.rank = rank;
        const auto inner_count = std::floor(static_cast<long double>(usable) * p.inner_rate_hz / 6);
        result.minimum_inner_coefficients = ceil_to_u64(inner_count,
            "pattern coefficient count exceeds supported range");
        result.estimated_operations = estimated_operations(p, duration, per_attempt);
        result.operation_count_saturated = result.estimated_operations == UINT64_MAX;
        if (result.operation_count_saturated || result.estimated_operations > p.maximum_operations) {
            resource_exceeded = true;
            break;
        }
        if (margin > 0 && result.minimum_inner_coefficients >= p.minimum_inner_coefficients) {
            result.duration_found = true;
            break;
        }
        if (step == std::numeric_limits<std::uint64_t>::max()) break;
    }

    const bool assumptions_supported = p.noise_model_supported &&
        p.retained_energy_bound_supported && p.wrong_projection_bound_supported &&
        p.outage_bound_supported && p.equal_rank_fixed_bank_supported;
    if (resource_exceeded) result.status = PlannerStatus::computation_infeasible;
    else if (!result.duration_found) result.status = PlannerStatus::no_feasible_duration;
    else if (assumptions_supported) result.status = PlannerStatus::conditional_model_bound;
    else result.status = PlannerStatus::unsupported_assumptions;
    result.status_name = planner_status_name(result.status);
    return result;
}

const char* planner_status_name(PlannerStatus status) {
    switch (status) {
    case PlannerStatus::conditional_model_bound: return "conditional_model_bound";
    case PlannerStatus::unsupported_assumptions: return "unsupported_assumptions";
    case PlannerStatus::computation_infeasible: return "computation_infeasible";
    case PlannerStatus::no_feasible_duration: return "no_feasible_duration";
    }
    return "invalid_status";
}

} // namespace datapump::triad::statistics
