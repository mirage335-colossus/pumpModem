#include "datapump/triad_statistics.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace datapump::triad::statistics;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

PlannerInput supported() {
    PlannerInput p;
    p.cn0_db_hz = -8;
    p.target_correct_probability = .90;
    p.false_admission_probability_per_horizon = .01;
    p.search.monitoring_horizon_seconds = 12 * 3600;
    p.search.attempt_spacing_seconds = 60;
    p.search.timing_hypotheses = 21;
    p.search.epoch_hypotheses = 3;
    p.search.frequency_hypotheses = 5;
    p.search.duration_alternatives = 2;
    p.search.wrong_message_competitors = 2;
    p.coherence_block_seconds = 0;
    p.wrong_projection_fraction = .08;
    p.noise_model_supported = true;
    p.retained_energy_bound_supported = true;
    p.wrong_projection_bound_supported = true;
    p.outage_bound_supported = true;
    p.equal_rank_fixed_bank_supported = true;
    p.maximum_duration_seconds = 7 * 86400;
    return p;
}

void reference_checks() {
    const auto exact_one=gamma_null_threshold(1,std::log(100.),.01);
    check(!exact_one.conservative && std::abs(exact_one.score-std::log(10000.))<1e-12,
          "rank-one Gamma quantile differs from its exponential closed form");
    const auto tiny_one = gamma_null_threshold(1, 0, 1e-300);
    check(std::exp(-tiny_one.score) <= 1e-300,
          "rank-one threshold rounded below its requested tail");
    const auto fallback = gamma_null_threshold(100'000'001, std::log(1000.), 1e-4);
    check(fallback.conservative && fallback.score ==
          conservative_null_threshold(100'000'001, std::log(1000.), 1e-4).score,
          "unsupported numerical rank did not retain the concentration fallback");
    // This rank is inside the supported input boundary. At x ~= a, the
    // 20,000-term upper-incomplete-Gamma recurrence cannot reach its relative
    // convergence tolerance, so the documented conservative fallback applies.
    const auto exhausted = gamma_null_threshold(100'000'000, 0, .5);
    check(exhausted.conservative && exhausted.score ==
          conservative_null_threshold(100'000'000, 0, .5).score,
          "valid high-rank Gamma iteration exhaustion did not retain the conservative bound");
    const auto exact_two=gamma_null_threshold(2,0,.01);
    check(!exact_two.conservative &&
          std::abs(std::exp(-exact_two.score)*(1+exact_two.score)-.01)<1e-10,
          "rank-two Gamma quantile differs from its closed survival function");
    for(std::uint64_t rank:{3ULL,100ULL,100000ULL}){
        const auto q=gamma_null_threshold(rank,std::log(1000.),1e-4);
        check(!q.conservative && std::abs(gamma_log_survival(rank,q.score)-std::log(1e-7))<1e-5,
              "long-observation Gamma quantile failed tail inversion");
    }
    const auto one = conservative_null_threshold(1, 0, .05);
    const auto more = conservative_null_threshold(1, std::log(100.0), .05);
    const auto stricter = conservative_null_threshold(1, 0, .001);
    check(one.score > 1 && more.score > one.score && stricter.score > one.score,
          "conservative Gamma threshold is not monotone");
    check(std::abs(known_channel_binary_error(0, 1) - .5) < 1e-15,
          "identical known-channel codewords are not coin flips");
    check(std::abs(known_channel_binary_error(2, 1) - .158655253931457) < 1e-12,
          "known-channel binary error reference changed");
    check(std::abs(zero_event_upper_bound(100) - .02951305) < 1e-7,
          "zero-event exact one-sided bound changed");
    rejects([] { conservative_null_threshold(0, 0, .05); }, "rank zero accepted");
    rejects([] { conservative_null_threshold(1, -1, .05); }, "negative search log accepted");
    rejects([] { zero_event_upper_bound(0); }, "zero campaigns accepted");
}

void target_and_search_checks() {
    const auto p = supported();
    const auto plan = plan_duration(p);
    check(plan.status == PlannerStatus::conditional_model_bound && plan.duration_found,
          "supported model did not produce its conditional duration");
    check(plan.duration_seconds >= 8 && plan.duration_seconds >= 20 * p.dwell_seconds,
          "planner violated the minimum bit duration or dwell diversity");
    check(plan.minimum_inner_coefficients >= p.minimum_inner_coefficients,
          "planner omitted minimum pattern complexity");
    check(plan.total_search_hypotheses >= plan.attempts_per_horizon,
          "complete search bank omitted acquisition alternatives");
    check(plan.log_null_tests_per_horizon > std::log(12 * 3600 / 60.0),
          "false-admission calibration omitted repeated attempts");
    check(plan.separation_margin > 0 && plan.null_score_upper > 0 &&
          plan.wrong_score_upper > 0, "reported duration does not meet the bound");
    check(plan.required_wrong_log_tail >= plan.log_hypotheses_per_attempt +
          std::log(2 / (1 - p.target_correct_probability)) - 1e-12,
          "correct-reception bound omitted wrong-bit nuisance alternatives");

    auto unsupported = p;
    unsupported.wrong_projection_bound_supported = false;
    const auto unverified = plan_duration(unsupported);
    check(unverified.duration_found &&
          unverified.status == PlannerStatus::unsupported_assumptions,
          "unsupported model bounds were mislabeled as a guarantee");

    auto stricter = p;
    stricter.search.timing_hypotheses *= 4;
    const auto larger_bank = plan_duration(stricter);
    check(larger_bank.duration_seconds >= plan.duration_seconds &&
          larger_bank.log_null_tests_per_horizon > plan.log_null_tests_per_horizon,
          "larger acquisition search made the planner more optimistic");

    auto poor_outage = p;
    poor_outage.maximum_outage_fraction = .5;
    const auto outage_plan = plan_duration(poor_outage);
    check(outage_plan.duration_seconds >= plan.duration_seconds,
          "increased outage allowance reduced required elapsed duration");

    auto ambiguous = p;
    ambiguous.wrong_projection_fraction = .99;
    const auto no_separation = plan_duration(ambiguous);
    check(!no_separation.duration_found ||
          no_separation.duration_seconds >= plan.duration_seconds,
          "more ambiguous wrong-bit model reduced required duration");

    auto too_large = p;
    too_large.candidate_batch_size = 100000;
    too_large.sample_tile_size = 100000;
    check(plan_duration(too_large).status == PlannerStatus::computation_infeasible,
          "workspace ceiling was ignored");
    auto too_expensive = p;
    too_expensive.maximum_operations = 1;
    check(plan_duration(too_expensive).status == PlannerStatus::computation_infeasible,
          "operation budget was ignored");
    auto impossible = p;
    impossible.maximum_duration_seconds = 8;
    check(plan_duration(impossible).status == PlannerStatus::no_feasible_duration,
          "pattern-complexity minimum was not enforced");
    rejects([] { auto bad = supported(); bad.search.bit_hypotheses = 0; (void)plan_duration(bad); },
            "empty acquisition bank accepted");
}

void scaling_checks() {
    auto p = supported();
    p.cn0_db_hz = -18;
    p.search.monitoring_horizon_seconds = 86400;
    p.search.timing_hypotheses = 1;
    p.search.epoch_hypotheses = 1;
    p.search.frequency_hypotheses = 1;
    p.search.duration_alternatives = 1;
    p.search.wrong_message_competitors = 1;
    p.maximum_duration_seconds = 7 * 86400;
    p.maximum_operations = UINT64_MAX;
    const auto low = plan_duration(p);
    p.cn0_db_hz = -15;
    const auto high = plan_duration(p);
    check(low.duration_found && high.duration_found && high.duration_seconds < low.duration_seconds,
          "higher C/N0 did not reduce fixed-rank coherent duration");
    check(high.duration_seconds / low.duration_seconds > .25 &&
          high.duration_seconds / low.duration_seconds < .8,
          "fixed-rank coherent duration does not show inverse-energy scaling");

    p.coherence_block_seconds = 2;
    p.cn0_db_hz = -14;
    const auto segmented_low = plan_duration(p);
    p.cn0_db_hz = -11;
    const auto segmented_high = plan_duration(p);
    check(segmented_low.duration_found && segmented_high.duration_found &&
          segmented_high.duration_seconds < segmented_low.duration_seconds,
          "higher C/N0 did not reduce segmented duration");
    check(segmented_high.duration_seconds / segmented_low.duration_seconds > .25 &&
          segmented_high.duration_seconds / segmented_low.duration_seconds < .8,
          "segmented receiver duration failed its noncoherent scaling discriminator");
}

void long_span_checks() {
    auto p = supported();
    p.search.monitoring_horizon_seconds = 30 * 86400;
    p.search.attempt_spacing_seconds = 60;
    p.maximum_duration_seconds = 2 * 86400;
    p.baseband_samples_per_second = 960;
    p.candidate_batch_size = 64;
    p.sample_tile_size = 8192;
    const auto plan = plan_duration(p);
    check(plan.duration_found && plan.duration_seconds <= 2 * 86400,
          "multi-day monitoring horizon or duration was not planned");
    check(plan.estimated_workspace_bytes ==
          p.fixed_workspace_bytes + 64ULL * 8192 * 8 * 3,
          "streaming workspace grew with total observation duration");
    check(plan.log_null_tests_per_horizon > std::log(43200.0),
          "multi-day repeated acquisition schedule was omitted");
    const auto empirical = zero_event_upper_bound(30000);
    check(empirical < 1.1e-4 && empirical > .9e-4,
          "independent campaign count confidence bound is implausible");
}

void extreme_and_complexity_checks() {
    auto p = supported();
    p.search.timing_hypotheses = UINT64_MAX;
    p.search.epoch_hypotheses = UINT64_MAX;
    const auto huge = plan_duration(p);
    check(huge.hypothesis_count_saturated && huge.log_hypotheses_per_attempt > 88,
          "overflowing bank undercounted its logarithmic false-alarm budget");
    check(huge.status == PlannerStatus::computation_infeasible &&
          huge.operation_count_saturated,
          "overflowing operation estimate was treated as feasible");
    p = supported();
    p.candidate_batch_size = UINT64_MAX;
    p.sample_tile_size = 2;
    p.maximum_workspace_bytes = UINT64_MAX;
    const auto workspace = plan_duration(p);
    check(workspace.workspace_count_saturated &&
          workspace.status == PlannerStatus::computation_infeasible,
          "overflowing workspace was treated as feasible");
    p = supported();
    p.cn0_db_hz = 80;
    p.maximum_outage_fraction = .9;
    p.startup_overhead_seconds = 20;
    const auto erased = plan_duration(p);
    check(erased.duration_found && erased.useful_energy_seconds * p.inner_rate_hz / 6 >= 64,
          "elapsed time incorrectly supplied erased pattern complexity");
    check(known_channel_binary_error(1e308, 1e308) > .23,
          "known-channel benchmark overflowed its noise denominator");
    rejects([] { auto bad = supported(); bad.maximum_duration_seconds = 1e100;
                 (void)plan_duration(bad); }, "unrepresentable duration coordinate accepted");
    rejects([] { auto bad = supported(); bad.minimum_inner_coefficients = 1;
                 (void)plan_duration(bad); }, "minimum identifiable pattern complexity weakened");
    rejects([] { auto bad = supported(); bad.cn0_db_hz = 3080;
                 (void)plan_duration(bad); }, "overflowing noncentrality emitted NaN bounds");
    p = supported(); p.equal_rank_fixed_bank_supported = false;
    check(plan_duration(p).status == PlannerStatus::unsupported_assumptions,
          "uncertified mixed-rank bank claimed a correct-reception guarantee");
}

void duration_calendar_checks() {
    auto p = supported();
    p.cn0_db_hz = 80;
    p.duration_quantum_seconds = 16;
    p.duration_offset_seconds = -8;
    p.maximum_duration_seconds = 40;
    p.coherence_block_seconds = 2;
    const auto burst = plan_duration(p);
    check(burst.duration_found && burst.duration_seconds == 24 && burst.rank == 12,
          "burst duration was rounded off its physical calendar or used the wrong rank");
    check(burst.useful_energy_seconds == 24,
          "burst calendar did not evaluate energy at the selected elapsed duration");
    p.maximum_duration_seconds = 23.9;
    check(plan_duration(p).status == PlannerStatus::no_feasible_duration,
          "planner rounded a burst beyond the maximum duration");
    p.maximum_duration_seconds = 24;
    check(plan_duration(p).duration_seconds == 24,
          "inclusive maximum lost an exact burst duration");
    p.duration_quantum_seconds = 0;
    p.duration_offset_seconds = 0;
    const auto implicit = plan_duration(p);
    p.duration_quantum_seconds = p.dwell_seconds;
    const auto explicit_dwell = plan_duration(p);
    check(implicit.duration_seconds == explicit_dwell.duration_seconds,
          "zero duration quantum changed the legacy dwell grid");
    p.duration_quantum_seconds = 1.2;
    p.duration_offset_seconds = -.4;
    p.maximum_duration_seconds = 11.6;
    check(std::abs(plan_duration(p).duration_seconds - 11.6) < 1e-12,
          "floating dwell multiples lost their endpoint");
    rejects([] { auto bad = supported(); bad.duration_quantum_seconds = .6;
                 (void)plan_duration(bad); }, "fractional-dwell duration quantum accepted");
    rejects([] { auto bad = supported(); bad.duration_quantum_seconds = 16;
                 bad.duration_offset_seconds = -.2; (void)plan_duration(bad); },
            "fractional-dwell duration offset accepted");
    rejects([] { auto bad = supported(); bad.duration_quantum_seconds = 16;
                 bad.duration_offset_seconds = -16; (void)plan_duration(bad); },
            "zero first duration accepted");
    rejects([] { auto bad = supported(); bad.duration_offset_seconds = .4;
                 (void)plan_duration(bad); }, "positive duration offset accepted");
}
void bounded_search_horizon_checks() {
    const auto defaults = plan_duration(PlannerInput{});
    check(defaults.duration_found && defaults.status == PlannerStatus::unsupported_assumptions,
          "long default horizon rejected an early conditional solution");
    auto short_input = supported();
    const auto short_plan = plan_duration(short_input);
    short_input.maximum_duration_seconds = 10 * 365.25 * 86400;
    const auto extended = plan_duration(short_input);
    check(extended.duration_seconds == short_plan.duration_seconds &&
          extended.separation_margin == short_plan.separation_margin,
          "extending horizon changed the first feasible duration");
    auto exhausted = short_input;
    exhausted.cn0_db_hz = -100;
    bool explicit_exhaustion = false;
    try { (void)plan_duration(exhausted); }
    catch (const std::invalid_argument& error) {
        explicit_exhaustion = std::string(error.what()).find("unvisited durations remain") != std::string::npos;
    }
    check(explicit_exhaustion, "bounded search concealed unvisited durations as physical infeasibility");
}
} // namespace

int main() {
    try {
        reference_checks();
        target_and_search_checks();
        scaling_checks();
        long_span_checks();
        extreme_and_complexity_checks();
        duration_calendar_checks();
        bounded_search_horizon_checks();
        std::cout << "triad statistics: conditional bounds, planner, scaling, and long-span checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
