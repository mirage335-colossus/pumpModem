#include "datapump/triad_statistics.hpp"
#include "datapump/triad_modem_v3.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Fixed, complete planner workloads. Repetitions amortize the process timer;
// they are never statistical replicates. No waveform is generated here.
namespace {
namespace st = datapump::triad::statistics;
namespace v3 = datapump::triad::v3;
struct Output {
    std::string name, status;
    double value = 0;
    std::uint64_t coordinate = 0;
};
using Outputs = std::vector<Output>;
void check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
st::PlannerInput supported() {
    st::PlannerInput p;
    p.cn0_db_hz = -8;
    p.target_correct_probability = .9;
    p.false_admission_probability_per_horizon = .01;
    p.search.monitoring_horizon_seconds = 43200;
    p.search.timing_hypotheses = 21;
    p.search.epoch_hypotheses = 3;
    p.search.frequency_hypotheses = 5;
    p.search.duration_alternatives = 2;
    p.wrong_projection_fraction = .08;
    p.noise_model_supported = p.retained_energy_bound_supported = true;
    p.wrong_projection_bound_supported = p.outage_bound_supported = true;
    p.equal_rank_fixed_bank_supported = true;
    p.maximum_duration_seconds = 7 * 86400;
    return p;
}
void planned(Outputs& out, const char* name, const st::PlannerInput& p,
             st::PlannerStatus expected) {
    const auto r = st::plan_duration(p);
    check(r.status == expected, "planner status changed");
    out.push_back({name, r.status_name, r.duration_seconds, r.rank});
}
Outputs early() {
    Outputs out;
    auto p = supported(); p.cn0_db_hz = 80;
    planned(out, "supported", p, st::PlannerStatus::conditional_model_bound);
    p.maximum_duration_seconds = 10 * 365.25 * 86400;
    planned(out, "extended_horizon_early_solution", p, st::PlannerStatus::conditional_model_bound);
    p.wrong_projection_bound_supported = false;
    planned(out, "uncertified", p, st::PlannerStatus::unsupported_assumptions);
    p = supported(); p.cn0_db_hz = 80; p.duration_quantum_seconds = 16;
    p.duration_offset_seconds = -8; p.maximum_duration_seconds = 40;
    p.coherence_block_seconds = 2;
    planned(out, "burst_calendar", p, st::PlannerStatus::conditional_model_bound);
    check(out.back().value == 24 && out.back().coordinate == 12, "burst calendar changed");
    p.maximum_duration_seconds = 23.9;
    planned(out, "exclusive_calendar_endpoint", p, st::PlannerStatus::no_feasible_duration);
    p.maximum_duration_seconds = 24;
    planned(out, "inclusive_calendar_endpoint", p, st::PlannerStatus::conditional_model_bound);
    return out;
}
Outputs grids() {
    Outputs out;
    auto p = supported(); p.cn0_db_hz = -100; p.maximum_duration_seconds = 4096;
    planned(out, "fixed_rank_complete_grid", p, st::PlannerStatus::no_feasible_duration);
    check(out.back().value == 4096, "fixed-rank grid did not reach its endpoint");
    p.coherence_block_seconds = 2;
    planned(out, "segmented_complete_grid", p, st::PlannerStatus::no_feasible_duration);
    check(out.back().value == 4096 && out.back().coordinate == 2048,
          "segmented grid did not reach its endpoint");
    return out;
}
Outputs resources() {
    Outputs out; auto p = supported();
    p.candidate_batch_size = 100000; p.sample_tile_size = 100000;
    planned(out, "workspace_limit", p, st::PlannerStatus::computation_infeasible);
    p = supported(); p.maximum_operations = 1;
    planned(out, "operation_limit", p, st::PlannerStatus::computation_infeasible);
    p = supported(); p.search.timing_hypotheses = UINT64_MAX; p.search.epoch_hypotheses = UINT64_MAX;
    const auto bank = st::plan_duration(p);
    check(bank.hypothesis_count_saturated && bank.operation_count_saturated &&
          bank.status == st::PlannerStatus::computation_infeasible, "bank saturation changed");
    out.push_back({"bank_saturation", bank.status_name, bank.duration_seconds, bank.total_search_hypotheses});
    p = supported(); p.candidate_batch_size = UINT64_MAX; p.sample_tile_size = 2;
    p.maximum_workspace_bytes = UINT64_MAX;
    const auto space = st::plan_duration(p);
    check(space.workspace_count_saturated && space.status == st::PlannerStatus::computation_infeasible,
          "workspace saturation changed");
    out.push_back({"workspace_saturation", space.status_name, space.duration_seconds, space.estimated_workspace_bytes});
    p = supported(); p.maximum_duration_seconds = 8;
    planned(out, "minimum_complexity", p, st::PlannerStatus::no_feasible_duration);
    return out;
}
Outputs exhaustion() {
    auto p = supported(); p.cn0_db_hz = -100;
    p.maximum_duration_seconds = 10 * 365.25 * 86400;
    try { (void)st::plan_duration(p); }
    catch (const std::invalid_argument& e) {
        check(std::string(e.what()).find("unvisited durations remain") != std::string::npos,
              "planner threw an unrelated exception");
        // This is the declared branch limit, not an instrumented iteration count.
        return {{"exhaustive_grid_cap", "explicit_exhaustion", 0, 2000001}};
    }
    throw std::runtime_error("planner concealed unvisited grid points");
}
Outputs geometry() {
    Outputs out;
    auto append = [&](const char* name, const v3::Format& f) {
        const auto g = v3::geometry(f);
        out.push_back({name, "complete", g.bit_seconds, g.source_samples});
        return g;
    };
    v3::Format f;
    append("ordinary", f);
    f.fhss = true; f.inner_rate_hz = 360; f.sample_rate = 9600;
    append("protected_fhss", f);
    f = {}; f.inner_rate_hz = .000036; f.added_symbols = 1;
    f.sample_rate = 1; f.bit_seconds = 38 * 86400.;
    const auto month = append("multi_month_minimum_tile", f);
    check(month.tile_seconds > 185 * 86400. && month.tile_seconds < 186 * 86400.,
          "multi-month minimum tile changed");
    f.bit_seconds = 2 * month.tile_seconds;
    check(append("exact_tile_endpoint", f).tile_count == 2, "exact tile endpoint changed");
    f.bit_seconds = (2 + 1e-6) * month.tile_seconds;
    check(append("fractional_tile_endpoint", f).tile_count == 3, "fractional tile endpoint changed");
    f.inner_rate_hz = .000000036; f.bit_seconds = 8;
    const auto century = append("wide_coordinate", f);
    check(century.dwells > UINT32_MAX && century.source_samples > UINT32_MAX &&
          century.source_workspace_bytes == month.source_workspace_bytes,
          "64-bit calendar or bounded workspace changed");
    f.inner_rate_hz = .036; f.minimum_inner_coefficients = 1000000;
    bool rejected = false;
    try { (void)v3::geometry(f); } catch (const std::exception&) { rejected = true; }
    check(rejected, "tile workspace guard changed");
    out.push_back({"workspace_boundary", "rejected", 0, 0});
    return out;
}
Outputs gamma() {
    Outputs out;
    for (const std::uint64_t rank : {1ULL, 2ULL, 3ULL, 100ULL, 100000ULL}) {
        const auto q = st::gamma_null_threshold(rank, std::log(1000.), 1e-4);
        check(!q.conservative && std::abs(st::gamma_log_survival(rank, q.score) - std::log(1e-7)) < 1e-5,
              "exact Gamma tail inversion changed");
        out.push_back({"rank_" + std::to_string(rank), "numerical_or_closed_form", q.score, rank});
    }
    const auto unsupported = st::gamma_null_threshold(100000001, std::log(1000.), 1e-4);
    const auto exhausted = st::gamma_null_threshold(100000000, 0, .5);
    check(unsupported.conservative && exhausted.conservative, "Gamma fallback changed");
    out.push_back({"unsupported_rank", "conservative", unsupported.score, 100000001});
    out.push_back({"iteration_exhaustion", "conservative", exhausted.score, 100000000});
    out.push_back({"known_channel_binary", "complete", st::known_channel_binary_error(2, 1), 0});
    out.push_back({"zero_event_bound", "complete", st::zero_event_upper_bound(30000), 30000});
    return out;
}
using Operation = Outputs (*)();
Operation operation(const std::string& name) {
    if (name == "early-calendar") return early;
    if (name == "finite-grids") return grids;
    if (name == "resource-exits") return resources;
    if (name == "grid-exhaustion") return exhaustion;
    if (name == "geometry-boundaries") return geometry;
    if (name == "gamma-branches") return gamma;
    throw std::invalid_argument("unknown planner performance case");
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::string name; unsigned repetitions = 1;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--case" && i + 1 < argc) name = argv[++i];
            else if (arg == "--repetitions" && i + 1 < argc) {
                const std::string value = argv[++i]; std::size_t used = 0;
                const auto count = std::stoul(value, &used);
                check(used == value.size() && count > 0 && count <= 1000000,
                      "invalid planner repetitions");
                repetitions = static_cast<unsigned>(count);
            } else throw std::invalid_argument("usage: triad_planner_perf --case NAME [--repetitions N]");
        }
        const auto run = operation(name);
        Outputs first; double checksum = 0; std::uint64_t scenarios = 0;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < repetitions; ++i) {
            auto values = run();
            for (const auto& v : values) checksum += v.value + static_cast<double>(v.coordinate);
            scenarios += values.size();
            if (i == 0) first = std::move(values);
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        check(std::isfinite(checksum), "non-finite output checksum");
        std::cout << std::setprecision(17)
            << "{\"schema\":\"triad-planner-perf-native-v1\",\"case\":\"" << name
            << "\",\"driver_sha256\":\"" << TRIAD_PLANNER_DRIVER_SHA256
            << "\",\"production_sha256\":\"" << TRIAD_PLANNER_PRODUCTION_SHA256
            << "\",\"repetitions\":" << repetitions << ",\"scenario_evaluations\":" << scenarios
            << ",\"checksum\":" << checksum << ",\"operation_seconds\":" << elapsed
            << ",\"arithmetic\":\"fp64_and_native_long_double_bounds\",\"outputs\":[";
        for (std::size_t i = 0; i < first.size(); ++i) {
            const auto& value = first[i];
            if (i) std::cout << ',';
            std::cout << "{\"name\":\"" << value.name << "\",\"status\":\"" << value.status
                      << "\",\"value\":" << value.value << ",\"coordinate\":" << value.coordinate << '}';
        }
        std::cout << "],\"internal_iterations_instrumented\":false,\"complete\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "triad-planner-perf: " << error.what() << '\n'; return 2;
    }
}
