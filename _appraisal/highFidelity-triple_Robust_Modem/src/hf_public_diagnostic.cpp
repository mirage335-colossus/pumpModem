#include "appraisal/hf_public_diagnostic.hpp"
#include "appraisal/hf_public_receiver.hpp"
#include "datapump/execution_resources.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_stream_resources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace appraisal {
namespace {
using namespace datapump::triad::v3;
using namespace receiver_detail;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using Clock = std::chrono::steady_clock;
constexpr double negative_infinity = -std::numeric_limits<double>::infinity();
constexpr std::uint64_t maximum_total_fft_row_stages = 40'000'000'000ULL;
constexpr std::uint64_t maximum_refinement_lookups=2'000'000'000ULL;
constexpr std::uint64_t maximum_refinement_direct_rows=100'000'000ULL;
constexpr std::size_t maximum_refinement_timings=6;
constexpr std::size_t maximum_timing_atoms = 512, render_tile_rows = 4096;
// Canonical preparation bounds: <=64 ordinary, <=24 varying, <=8 segmented,
// <=34 epoch timing atoms, before removing their intentional overlap.
constexpr std::size_t maximum_selected_timing_atoms = 130;
constexpr double comparison_slope_bound = .013, phase_error_target = .25;
constexpr double supplement_weight = .25;
constexpr const char* arithmetic = "native configured coarse proposals; shared public FP64 phase rotations and DoublePlan FFT; FP64 complex block sums; long-double template-energy and likelihood reductions";
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now()-start).count();
}
void require(bool condition, const char* text) { if (!condition) throw datapump::Error(text); }
struct PublicGeometryError : datapump::Error { using datapump::Error::Error; };
void geometry_require(bool condition,const char* text) { if (!condition) throw PublicGeometryError(text); }
void add_work(HfPhaseRefinementWork& sum,const HfPhaseRefinementWork& term,std::uint64_t count=1) {
    constexpr std::array members{&HfPhaseRefinementWork::fft_row_stages,&HfPhaseRefinementWork::spectrum_cells,
        &HfPhaseRefinementWork::table_lookups,&HfPhaseRefinementWork::direct_sweeps,&HfPhaseRefinementWork::direct_rows,
        &HfPhaseRefinementWork::phase_rotations,&HfPhaseRefinementWork::trigonometric_evaluations,
        &HfPhaseRefinementWork::likelihood_cells};
    for (const auto member:members) {
        geometry_require(!count||(term.*member)<=(UINT64_MAX-(sum.*member))/count,"HF refinement work-count overflow");
        sum.*member+=(term.*member)*count;
    }
}
Json work_json(const HfPhaseRefinementWork& w) {
    return object({{"fft_row_stages",w.fft_row_stages},{"spectrum_cells",w.spectrum_cells},
        {"table_lookups",w.table_lookups},{"direct_sweeps",w.direct_sweeps},{"direct_rows",w.direct_rows},
        {"phase_rotations",w.phase_rotations},{"trigonometric_evaluations",w.trigonometric_evaluations},
        {"likelihood_cells",w.likelihood_cells}});
}

double log_add(double a, double b) {
    if (a==negative_infinity) return b;
    if (b==negative_infinity) return a;
    return std::max(a,b)+std::log1p(std::exp(-std::abs(a-b)));
}
Json coordinate_json(const FineCoordinate& c) {
    return object({{"epoch_offset_seconds",c.epoch_offset_seconds},
        {"arrival_seconds",c.arrival_seconds},{"clock_ppm",c.clock_ppm}});
}
AdmissionGrid timing_grid(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    return {slot.arrival_lower_seconds,slot.arrival_upper_seconds,0,config.clock_bound_ppm,.01};
}
std::size_t timing_count(const AdmissionGrid& grid) {
    geometry_require(grid.arrival_intervals<maximum_timing_atoms,"HF public timing lattice exceeds diagnostic cap");
    const auto arrivals=static_cast<std::size_t>(grid.arrival_intervals)+1;
    const auto count=checked_multiply(checked_multiply(receiver_epoch_offsets.size(),arrivals),grid.clock_bound?9:1);
    geometry_require(count<=maximum_timing_atoms,"HF public timing lattice exceeds diagnostic cap");
    return count;
}
bool public_geometry_supported(const ReceiverConfig& config,const ReceiverSlotPlan& slot) {
    if (slot.stop_sample<=slot.first_sample||slot.stop_sample-slot.first_sample>SIZE_MAX||
        config.helper_enabled||config.bulk_backend||config.spread_delay_seconds!=0||config.spread_doppler_hz!=0||
        !(config.noise_variance>0)||!std::isfinite(config.noise_variance)||
        !(config.sample_rate_hz>0)||!std::isfinite(config.sample_rate_hz)) return false;
    const double duration=(slot.stop_sample-slot.first_sample)/config.sample_rate_hz;
    const double bound=config.frequency_bound_hz+config.time_varying_atmospheric_hz;
    if (!std::isfinite(duration)||duration>600||!std::isfinite(bound)||bound<0||bound>25) return false;
    // Validate every AdmissionGrid input before its construction. This cheap
    // predicate performs no allocation and does not inspect lane resources.
    if (!std::isfinite(slot.arrival_lower_seconds)||!std::isfinite(slot.arrival_upper_seconds)||
        slot.arrival_upper_seconds<slot.arrival_lower_seconds||
        !std::isfinite(slot.arrival_upper_seconds-slot.arrival_lower_seconds)||
        slot.arrival_upper_seconds-slot.arrival_lower_seconds>.512||
        !std::isfinite(config.clock_bound_ppm)||config.clock_bound_ppm<0||config.clock_bound_ppm>=1e6)
        return false;
    return true;
}

struct SearchPlan {
    HfPublicStatisticsPlan statistics;
    std::optional<HfPhaseRefinementPlan> refinement;
    HfPhaseRefinementWork refinement_work_bound;
    std::string refinement_unavailable_reason;
    std::size_t retained_statistics=0;
    std::size_t full_count=0, block_rows=0, tile=0, basis_workspace=0, pair_workspace=0, workspace=0, metadata=0;
    unsigned active=0;
    double phase_error=0;
};
SearchPlan numerical_search_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                      std::size_t evaluated_timing,bool include_affine,bool phase_refinement) {
    geometry_require(!phase_refinement||!include_affine,"refined HF search requires operational block geometry");
    geometry_require(public_geometry_supported(config,slot),
            "HF public search requires supported public geometry");
    SearchPlan result;
    result.full_count=timing_count(timing_grid(config,slot));
    geometry_require(evaluated_timing>0&&evaluated_timing<=result.full_count,"invalid HF public timing work count");
    const auto rows=static_cast<std::size_t>(slot.stop_sample-slot.first_sample);
    const double fs=config.sample_rate_hz, duration=rows/fs;
    const double bound=config.frequency_bound_hz+config.time_varying_atmospheric_hz;
    // Solve both derivative bounds before allocating. The .01 allowance
    // covers the outward CFO grid rounding; the affine inequality also
    // includes a partial block's nominal center beyond the aperture end.
    double maximum_block_seconds=phase_error_target/(std::numbers::pi*(bound+.01+16./90.));
    if (include_affine) {
        const double a=std::numbers::pi*comparison_slope_bound/2;
        const double b=std::numbers::pi*(bound+.01+comparison_slope_bound*duration/2);
        const double affine_seconds=2*phase_error_target/(b+std::sqrt(b*b+4*a*phase_error_target));
        maximum_block_seconds=std::min(maximum_block_seconds,affine_seconds);
    }
    const auto proposed=std::floor(maximum_block_seconds*fs);
    geometry_require(std::isfinite(proposed)&&static_cast<long double>(proposed)<static_cast<long double>(SIZE_MAX),"HF public block length overflow");
    result.block_rows=std::max<std::size_t>(1,static_cast<std::size_t>(proposed));
    HfPublicStatisticsConfig c;
    c.block_count=1+(rows-1)/result.block_rows;
    c.block_seconds=result.block_rows/fs;
    c.first_block_center_seconds=static_cast<double>((static_cast<long double>(slot.first_sample)+(result.block_rows-1)*.5L)/fs);
    c.aperture_seconds=duration;
    c.aperture_midpoint_seconds=static_cast<double>((static_cast<long double>(slot.first_sample)+(rows-1)*.5L)/fs);
    c.white_noise_variance=config.noise_variance;
    c.frequency_bound_hz=bound;
    c.affine_slope_bound_hz_per_second=comparison_slope_bound;
    c.include_affine=include_affine;
    c.maximum_fft_rows=1U<<20;
    c.maximum_workspace_bytes=SIZE_MAX;
    result.statistics=hf_public_statistics_plan(c);
    const auto& p=result.statistics;
    const auto extra_frequency=include_affine?std::max(16./90.,comparison_slope_bound*(duration+c.block_seconds)/2):16./90.;
    result.phase_error=std::numbers::pi*c.block_seconds*(p.actual_frequency_bound_hz+extra_frequency);
    geometry_require(std::isfinite(result.phase_error)&&result.phase_error<=phase_error_target*(1+64*std::numeric_limits<double>::epsilon()),
            "HF public effective-template phase error exceeds the declared bound");
    geometry_require(p.actual_fft_row_stages<=maximum_total_fft_row_stages/evaluated_timing,
            "HF public phase search exceeds its predeclared total FFT work cap");
    if (phase_refinement) {
        // Availability is determined over the full public timing lattice,
        // before coarse proposals or IQ can select a cheaper timing subset.
        // Failure of this optional family never removes the supported old97
        // family or reclaims the missing half of its fixed mixture prior.
        std::optional<HfPhaseRefinementPlan> candidate;
        try { candidate=hf_phase_refinement_plan(p); }
        catch (const std::invalid_argument&) {
            result.refinement_unavailable_reason="new_phase_planner_unsupported";
        }
        if (candidate) {
            try {
                add_work(result.refinement_work_bound,candidate->rank_work,result.full_count);
                add_work(result.refinement_work_bound,candidate->refinement_work,
                         std::min(maximum_refinement_timings,result.full_count));
            } catch (const PublicGeometryError&) {
                result.refinement_work_bound={};
                result.refinement_unavailable_reason="new_phase_work_count_overflow";
            }
            const auto& work=result.refinement_work_bound;
            if (result.refinement_unavailable_reason.empty()) {
                if (p.actual_fft_row_stages>maximum_total_fft_row_stages/result.full_count||
                    work.fft_row_stages>maximum_total_fft_row_stages-p.actual_fft_row_stages*result.full_count)
                    result.refinement_unavailable_reason="new_phase_fft_work_cap";
                else if (work.table_lookups>maximum_refinement_lookups)
                    result.refinement_unavailable_reason="new_phase_table_lookup_cap";
                else if (work.direct_rows>maximum_refinement_direct_rows)
                    result.refinement_unavailable_reason="new_phase_direct_row_cap";
            }
            if (result.refinement_unavailable_reason.empty()) {
                result.refinement=std::move(candidate);
                result.refinement_work_bound={};
                add_work(result.refinement_work_bound,result.refinement->rank_work,evaluated_timing);
                add_work(result.refinement_work_bound,result.refinement->refinement_work,
                         std::min(maximum_refinement_timings,evaluated_timing));
                result.retained_statistics=checked_multiply(evaluated_timing,
                    checked_multiply(checked_multiply(2,c.block_count),sizeof(Complex)+sizeof(double)));
            }
        }
    }
    return result;
}
SearchPlan search_plan(const ReceiverConfig& config,const ReceiverSlotPlan& slot,
                       unsigned workers,std::size_t evaluated_timing,bool include_affine,bool phase_refinement=false) {
    require(workers>0,"HF public search requires positive workers");
    auto result=numerical_search_plan(config,slot,evaluated_timing,include_affine,phase_refinement);
    const auto rows=static_cast<std::size_t>(slot.stop_sample-slot.first_sample);
    const auto& p=result.statistics;
    const auto& c=p.config;
    result.active=static_cast<unsigned>(std::min<std::size_t>(workers,evaluated_timing));
    result.tile=std::min(render_tile_rows,rows);
    result.basis_workspace=MappedBasis::workspace_bytes(config,slot,result.tile,result.active);
    // The paired caller buffer is conservatively charged again even though
    // MappedBasis's quote already includes paired-output space.
    result.pair_workspace=HfPublicBasisPair::workspace_bytes(config,slot);
    const auto q_scratch=result.refinement?0:checked_multiply(checked_multiply(2,c.block_count),sizeof(Complex));
    const auto fft_scratch=result.refinement?std::max(p.lane_workspace_bytes,result.refinement->rank_workspace_bytes):p.lane_workspace_bytes;
    const auto per_lane=checked_add(result.pair_workspace,checked_add(fft_scratch,checked_add(q_scratch,
        checked_multiply(result.tile,2*sizeof(Complex)+sizeof(double)))));
    const auto first_stage=checked_multiply(result.active,per_lane);
    const auto second_stage=result.refinement?checked_multiply(
        std::min<std::size_t>({workers,evaluated_timing,maximum_refinement_timings}),result.refinement->refinement_workspace_bytes):0;
    result.metadata=checked_add(65536,checked_multiply(result.full_count,32768));
    result.workspace=checked_add(result.metadata,checked_add(result.basis_workspace,
        checked_add(p.shared_workspace_bytes,checked_add(result.retained_statistics,std::max(first_stage,second_stage)))));
    return result;
}
HfPublicTimingSelection timing_subset(const ReceiverConfig& config,const ReceiverSlotPlan& slot,
                                        const FinePreparation& prepared,bool full_timing,bool close_small_arrival_grid) {
    const auto grid=timing_grid(config,slot);
    const auto full_count=timing_count(grid);
    require(prepared.grid.lower==grid.lower&&prepared.grid.upper==grid.upper&&prepared.grid.clock_bound==grid.clock_bound,
            "HF public preparation does not match configured timing geometry");
    HfPublicTimingSelection result;
    auto& timing=result.coordinates;
    timing.reserve(full_count);
    const auto append=[&](FineCoordinate coordinate) {
        require(std::ranges::find(receiver_epoch_offsets,coordinate.epoch_offset_seconds)!=receiver_epoch_offsets.end()&&
                std::isfinite(coordinate.arrival_seconds)&&std::isfinite(coordinate.clock_ppm),
                "HF public timing proposal is invalid");
        const double arrival_index=grid.arrival_intervals?
            std::round((coordinate.arrival_seconds-grid.lower)*grid.arrival_intervals/(grid.upper-grid.lower)):0;
        const double clock_index=grid.clock_bound?std::round((coordinate.clock_ppm+grid.clock_bound)*8/(2*grid.clock_bound)):0;
        require(arrival_index>=0&&arrival_index<=grid.arrival_intervals&&clock_index>=0&&clock_index<=8,
                "HF public timing proposal is outside the lattice");
        const double arrival=grid.arrival_intervals?grid.lower+arrival_index*((grid.upper-grid.lower)/grid.arrival_intervals):grid.lower;
        const double clock=grid.clock_bound?-grid.clock_bound+clock_index*(2*grid.clock_bound/8):0;
        const auto same=[](double a,double b) {return std::abs(a-b)<=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)});};
        require(same(arrival,coordinate.arrival_seconds)&&same(clock,coordinate.clock_ppm),
                "HF public timing proposal was not snapped to the fixed lattice");
        coordinate.arrival_seconds=arrival;coordinate.clock_ppm=clock;coordinate.frequency_hz=0;
        if (std::ranges::find(timing,coordinate)==timing.end()) timing.push_back(coordinate);
    };
    require(prepared.coordinates.size()<=64&&prepared.varying_coordinates.size()<=24&&
            prepared.segmented_timing_coordinates.size()<=8&&prepared.epoch_timing_coordinates.size()<=34,
            "HF public preparation exceeds the canonical coordinate bounds");
    for (const auto* bank:{&prepared.coordinates,&prepared.varying_coordinates,
                          &prepared.segmented_timing_coordinates,&prepared.epoch_timing_coordinates})
        for (const auto& coordinate:*bank) append(coordinate);
    require(!timing.empty()&&timing.size()<=maximum_selected_timing_atoms,
            "HF public canonical timing subset is invalid");
    result.canonical_selected_timing_atoms=timing.size();
    const auto arrivals=static_cast<std::size_t>(grid.arrival_intervals)+1;
    if (full_timing) {
        timing.clear();
        for (const auto epoch:receiver_epoch_offsets)
            for (std::size_t a=0;a<arrivals;++a)
                for (std::size_t c=0;c<(grid.clock_bound?9U:1U);++c)
                    append({epoch,arrivals==1?grid.lower:grid.lower+a*((grid.upper-grid.lower)/(arrivals-1)),0,
                        grid.clock_bound?-grid.clock_bound+c*(2*grid.clock_bound/8):0});
    } else if (close_small_arrival_grid&&arrivals<=3) {
        // This is closure over public cells, not a fitted timing refinement.
        // Keep the original denominator and retain only the observed coarse
        // epoch/clock keys. Iterating the original prefix avoids recursively
        // growing selection or adding independent timing hypotheses.
        for (std::size_t i=0;i<result.canonical_selected_timing_atoms;++i) {
            const auto retained=timing[i];
            for (std::size_t a=0;a<arrivals;++a) {
                auto coordinate=retained;
                coordinate.arrival_seconds=arrivals==1?grid.lower:
                    grid.lower+a*((grid.upper-grid.lower)/(arrivals-1));
                append(coordinate);
            }
        }
        result.arrival_grid_closed=true;
    }
    require(timing.size()<=full_count,"HF public timing subset is invalid");
    return result;
}

Json indices_json(std::span<const std::size_t> ids) {
    Json::Array a;for (const auto i:ids) a.emplace_back(i);return a;
}
Json shape_json(const HfPhaseShapeId& id) {return object({{"a",id.a},{"b",id.b},{"nu",id.nu}});}
Json unavailable_refinement_json(const std::array<double,2>& old,const std::array<double,2>& combined,
        const std::string& reason,const HfPhaseRefinementWork& attempted) {
    auto report=object({{"enabled",true},{"requested",true},{"executed",false},{"unavailable_reason",reason},
        {"old_family_weight",.5},{"new_family_weight",.5},
        {"old_bit_log_e",Json::Array{old[0],old[1]}},{"new_bit_log_e",Json::Array{negative_infinity,negative_infinity}},
        {"combined_bit_log_e",Json::Array{combined[0],combined[1]}},
        {"selected_timing_indices",Json::Array{}},{"old_top_timing_indices",Json::Array{}},
        {"segmented_top_timing_indices",Json::Array{}},{"timing_ranks",Json::Array{}},
        {"timing_results",Json::Array{}},{"selected_shape_counts",Json::Array{}},
        {"retained_new_prior_mass",0.},{"retained_statistics_workspace_bytes",0},
        {"work",work_json({})},{"second_stage_wall_seconds",0.},{"summed_worker_wall_seconds",0.},
        {"work_bound_scope","full public timing preflight; unavailable family was not admitted or executed"}});
    if (attempted.fft_row_stages) report["work_bound"]=work_json(attempted);
    return report;
}
Json refinement_json(const HfPublicSearchResult& r) {
    if (!r.refinement_enabled) return object({{"enabled",false}});
    if (!r.refinement_executed) return unavailable_refinement_json(r.bit_log_e[2],r.combined_phase_bit_log_e,
        r.refinement_unavailable_reason,r.refinement_work_bound);
    const auto& p=*r.refinement_plan;
    Json::Array ranks,results;
    double retained_mass=0;
    for (std::size_t i=0;i<r.timing.size();++i) {
        const auto& old=r.evidence[i].statistics.families[2].hypotheses;
        const auto& segmented=r.refinement_ranks[i];
        ranks.push_back(object({{"timing_index",i},
            {"old_presence_rank",log_add(old[0].log_likelihood_ratio,old[1].log_likelihood_ratio)-std::numbers::ln2},
            {"segmented_presence_rank",log_add(segmented.bit_scores[0],segmented.bit_scores[1])-std::numbers::ln2},
            {"segmented_bit_scores",Json::Array{segmented.bit_scores[0],segmented.bit_scores[1]}}}));
    }
    for (const auto& entry:r.refinement_evidence) {
        const auto& e=entry.report;Json::Array shapes,proposals;
        retained_mass+=e.retained_shape_prior_mass/r.public_timing_atoms;
        for (std::size_t i=0;i<e.selected_count;++i) shapes.push_back(shape_json(e.selected_shapes[i]));
        for (std::size_t i=0;i<e.proposal_count;++i) {
            const auto& v=e.proposals[i];
            proposals.push_back(object({{"a",v.a},{"b",v.b},{"nu_hz",v.nu_hz},{"cfo_hz",v.cfo_hz},
                {"bit",v.bit},{"segment_score",v.segment_score},{"initial_log_lr",v.initial_log_lr},
                {"continuous_log_lr",v.continuous_log_lr},{"best_scored_neighbor_log_lr",v.best_scored_neighbor_log_lr},
                {"intermediate_used",v.intermediate_used},
                {"intermediate_initial_log_lr",v.intermediate_initial_log_lr},
                {"intermediate_final_log_lr",v.intermediate_final_log_lr},
                {"intermediate_accepted_steps",v.intermediate_accepted_steps},
                {"snapped_loss_nats",v.snapped_loss_nats},{"accepted_steps",v.accepted_steps}}));
        }
        results.push_back(object({{"timing_index",entry.timing_index},{"bit_log_e",Json::Array{e.bit_log_e[0],e.bit_log_e[1]}},
            {"maximum_log_lr",Json::Array{e.maximum_log_lr[0],e.maximum_log_lr[1]}},
            {"maximum_frequency_hz",Json::Array{e.maximum_frequency_hz[0],e.maximum_frequency_hz[1]}},
            {"maximum_shape",Json::Array{shape_json(e.maximum_shape[0]),shape_json(e.maximum_shape[1])}},
            {"selected_count",e.selected_count},{"selected_shapes",std::move(shapes)},{"proposal_count",e.proposal_count},
            {"proposals",std::move(proposals)},{"retained_shape_prior_mass",e.retained_shape_prior_mass},
            {"log_cell_prior",e.log_cell_prior},{"workspace_bytes",e.workspace_bytes},{"work",work_json(e.work)}}));
    }
    return object({{"enabled",true},{"requested",true},{"executed",true},{"unavailable_reason",""},
        {"old_family_weight",.5},{"new_family_weight",.5},
        {"old_bit_log_e",Json::Array{r.bit_log_e[2][0],r.bit_log_e[2][1]}},
        {"new_bit_log_e",Json::Array{r.new_phase_bit_log_e[0],r.new_phase_bit_log_e[1]}},
        {"combined_bit_log_e",Json::Array{r.combined_phase_bit_log_e[0],r.combined_phase_bit_log_e[1]}},
        {"shape_count",p.shape_count},{"nu_count",p.nu_count},{"nu_step_hz",p.nu_step_hz},
        {"segment_count",p.segment_count},{"segment_rows",p.segment_rows},{"segment_fft_rows",p.segment_fft_rows},
        {"intermediate_group_rows",p.intermediate_group_rows},{"intermediate_group_count",p.intermediate_group_count},
        {"intermediate_iteration_limit",p.intermediate_group_count?HfPhaseRefinementPlan::intermediate_iterations:0},
        {"coherent_iteration_limit",HfPhaseRefinementPlan::maximum_iterations-
            (p.intermediate_group_count?HfPhaseRefinementPlan::intermediate_iterations:0)},
        {"segment_frequency_step_hz",p.segment_frequency_step_hz},
        {"coarse_nu_count",p.coarse_nu_count},{"coarse_frequency_step_hz",p.coarse_frequency_step_hz},
        {"coarse_frequency_radius",p.coarse_frequency_radius},
        {"public_timing_atoms",r.public_timing_atoms},{"frequency_cells",r.plan.frequency_count},
        {"log_full_cell_count",std::log(static_cast<double>(r.public_timing_atoms))+std::log(static_cast<double>(p.shape_count))+
            std::log(static_cast<double>(r.plan.frequency_count))},
        {"retained_new_prior_mass",retained_mass},{"selected_timing_indices",indices_json(r.refinement_frontier.selected)},
        {"old_top_timing_indices",indices_json(r.refinement_frontier.old_top)},
        {"segmented_top_timing_indices",indices_json(r.refinement_frontier.segmented_top)},
        {"timing_ranks",std::move(ranks)},{"timing_results",std::move(results)},
        {"rank_use","proposal selection only; segmented maxima and continuous fits supply no evidence"},
        {"work_bound",work_json(r.refinement_work_bound)},{"work",work_json(r.refinement_work)},
        {"retained_statistics_workspace_bytes",r.retained_statistics_workspace_bytes},
        {"rank_workspace_bytes_per_lane",p.rank_workspace_bytes},{"refinement_workspace_bytes_per_lane",p.refinement_workspace_bytes},
        {"maximum_table_lookups",maximum_refinement_lookups},{"maximum_direct_rows",maximum_refinement_direct_rows},
        {"second_stage_wall_seconds",r.refinement_wall_seconds},
        {"summed_worker_wall_seconds",r.refinement_summed_worker_wall_seconds}});
}

Json summary_json(const HfPublicSearchResult& r) {
    const auto& p=r.plan;
    return object({{"public_timing_atoms",r.public_timing_atoms},{"evaluated_timing_atoms",r.timing.size()},
        {"refinement_enabled",r.refinement_enabled},{"affine_comparison_evaluated",r.plan.config.include_affine},
        {"phase_refinement",refinement_json(r)},
        {"effective_template_phase_error_bound_scope","fixed97, radius16 Cartesian refinement, and affine comparison when enabled; excludes phase-grid/model mismatch"},
        {"assumed_public_taps",1},{"channel_model_scope","actual channel may be mismatched; no calibrated multipath-recovery claim"},
        {"canonical_selected_timing_atoms",r.canonical_selected_timing_atoms},{"arrival_grid_closed",r.arrival_grid_closed},
        {"retained_timing_prior_mass",static_cast<double>(r.timing.size())/r.public_timing_atoms},
        {"selected_timing_renormalized",false},{"frequency_cells",p.frequency_count},
        {"frequency_step_hz",p.frequency_step_hz},{"configured_frequency_bound_hz",p.config.frequency_bound_hz},
        {"actual_frequency_bound_hz",p.actual_frequency_bound_hz},{"block_rows",r.block_rows},
        {"blocks",p.config.block_count},{"fft_rows",p.fft_rows},{"stage_workers",r.stage_workers},
        {"effective_template_phase_error_bound_radians",r.phase_error_bound_radians},
        {"ideal_to_effective_coherence_power_lower_bound",std::pow(std::cos(r.phase_error_bound_radians),2)},
        {"search_workspace_bytes",r.workspace_bytes},{"basis_workspace_bytes",r.basis_workspace_bytes},
        {"metadata_reserve_bytes",r.metadata_reserve_bytes},
        {"statistics_actual_workspace_peak_bytes_per_lane",r.statistics_actual_workspace_peak_bytes},
        {"statistics_shared_workspace_bytes",r.statistics_shared_workspace_bytes},
        {"statistics_shared_workspace_quote_bytes",p.shared_workspace_bytes},
        {"statistics_lane_workspace_quote_bytes",p.lane_workspace_bytes},
        {"statistics_preparation_seconds",r.statistics_preparation_seconds},
        {"statistics_phase_values_prepared",r.statistics_phase_values_prepared},
        {"statistics_summed_worker_wall_seconds",r.statistics_summed_worker_wall_seconds},
        {"basis_summed_worker_wall_seconds",r.basis_summed_worker_wall_seconds},
        {"summed_worker_wall_times_scope","overlapping per-timing elapsed intervals; not CPU time or complete service time"},
        {"actual_fft_row_stages",p.actual_fft_row_stages*r.timing.size()},
        {"total_fft_row_stages_including_refinement",p.actual_fft_row_stages*r.timing.size()+r.refinement_work.fft_row_stages},
        {"phase_complex_multiplications",p.phase_complex_multiplications*r.timing.size()},
        {"likelihood_cells",p.likelihood_cells*r.timing.size()},
        {"maximum_total_fft_row_stages",maximum_total_fft_row_stages},
        {"basis_render_rows",r.cache.interpolated_rows+r.cache.direct_rows+r.basis_pair.bank_direct_columns},
        {"basis_render_rows_units","rendered columns per call; historical field name, not sample count"},
        {"basis_direct_columns",r.cache.direct_rows+r.basis_pair.bank_direct_columns},
        {"basis_interpolated_columns",r.cache.interpolated_rows},
        {"basis_bank_calls",r.basis_pair.bank_calls},{"basis_bank_direct_columns",r.basis_pair.bank_direct_columns},
        {"basis_bank_column_samples",r.basis_pair.bank_column_samples},{"basis_fallback_calls",r.basis_pair.fallback_calls},
        {"basis_coordinate_guard_fallbacks",r.basis_pair.coordinate_guard_fallbacks},
        {"basis_pair_native_fallback_reason",r.basis_pair.native_fallback_reason},
        {"mapped_basis_fallback_reason",r.cache.fallback_reason},
        {"basis_pair_workspace_bytes_per_lane",r.basis_pair_workspace_bytes_per_lane},
        {"basis_cache_peak_bytes",r.cache.peak_cache_bytes},{"basis_cache_evictions",r.cache.evictions},
        {"search_seconds",r.search_seconds},{"public_search_arithmetic",arithmetic}});
}
} // namespace

struct HfPublicBasisPair::Impl {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    datapump::Crypto key;
    MappedBasisRow row;
    bool enable;
    std::array<std::unique_ptr<PatternSource>,2> sources;
    HfPublicBasisPairReport counters;
    Impl(const ReceiverConfig& c,const ReceiverSlotPlan& s,const datapump::Crypto& k,
         const MappedBasisRow& r,bool enabled):config(c),slot(s),key(k),row(r),enable(enabled) {}
};
HfPublicBasisPair::HfPublicBasisPair(const ReceiverConfig& c,const ReceiverSlotPlan& s,
        const datapump::Crypto& key,const MappedBasisRow& row,bool enable)
    :impl_(std::make_unique<Impl>(c,s,key,row,enable)) {}
HfPublicBasisPair::~HfPublicBasisPair()=default;
std::size_t HfPublicBasisPair::workspace_bytes(const ReceiverConfig& c,const ReceiverSlotPlan& s) {
    if (s.format.pulse_shape!=PulseShape::Rrc||effective_sample_rate(s.format)!=c.sample_rate_hz) return 4096;
    return checked_add(4096,checked_add(checked_multiply(2,geometry(s.format).source_workspace_bytes),
                                      PatternSource::render_bank_scratch_bytes));
}
void HfPublicBasisPair::render(MappedBasis& fallback,std::span<const Tap> taps,std::uint64_t first,
        std::span<const double> offsets,std::span<Complex> output,std::stop_token stop) {
    auto& p=*impl_;check_cancelled(stop);
    require(!offsets.empty()&&offsets.size()<=render_tile_rows&&!taps.empty()&&taps.size()<=2&&
        output.size()==2*taps.size()*offsets.size(),"HF native pair output shape mismatch");
    require(offsets.size()-1<=UINT64_MAX-first,"HF native pair sample coordinate overflow");
    const char* reason=nullptr;
    if (!p.enable) reason="reference_forced";
    else if (p.slot.format.pulse_shape!=PulseShape::Rrc||effective_sample_rate(p.slot.format)!=p.config.sample_rate_hz)
        reason="pulse_or_sample_rate_not_supported";
    else if (p.row.clock_ppm!=0||p.row.frequency_hz!=0) reason="clock_or_template_frequency_not_zero";
    else if (taps.size()!=1||taps[0].delay_seconds!=0||taps[0].doppler_hz!=0) reason="tap_geometry_not_supported";
    const auto shift_seconds=static_cast<long double>(p.row.arrival_seconds+0.);
    const auto shift=shift_seconds*p.config.sample_rate_hz;
    const auto origin=static_cast<long double>(first)-shift;
    long double previous=-1;
    if (!reason) for (std::size_t j=0;j<offsets.size();++j) {
        const auto index=static_cast<long double>(first)+offsets[j];
        require(index>previous,"HF native pair indices are not distinct at native precision");
        previous=index;
        if (offsets[j]!=static_cast<double>(j)) {reason="nonuniform_offsets";break;}
        if (origin+static_cast<long double>(j)!=(static_cast<long double>(first)+offsets[j])-shift) {
            reason="nonassociative_source_coordinates";++p.counters.coordinate_guard_fallbacks;break;
        }
    }
    if (reason) {
        if (p.counters.native_fallback_reason.empty()) p.counters.native_fallback_reason=reason;
        else if (p.counters.native_fallback_reason!=reason) p.counters.native_fallback_reason="multiple_bounded_fallback_reasons";
        ++p.counters.fallback_calls;
        fallback.render_pair(p.row,taps,first,offsets,output,stop);
        return;
    }
    require(std::isfinite(p.row.arrival_seconds)&&std::isfinite(p.config.data_if_hz)&&
        p.config.data_if_hz>=0&&p.config.data_if_hz<=120e6&&std::isfinite(p.config.data_basis_gain)&&p.config.data_basis_gain>=0,
        "HF native pair requires finite public map and gain");
    if (!p.sources[0]) {
        require(p.slot.format.epoch>=6&&p.slot.format.epoch<=UINT64_MAX-6&&
            p.row.epoch_offset_seconds>=-6&&p.row.epoch_offset_seconds<=6,"HF native pair epoch outside public alternatives");
        auto format=p.slot.format;
        format.epoch=p.row.epoch_offset_seconds<0?format.epoch-static_cast<std::uint64_t>(-p.row.epoch_offset_seconds):
            format.epoch+static_cast<std::uint64_t>(p.row.epoch_offset_seconds);
        std::array<std::unique_ptr<PatternSource>,2> prepared{
            std::make_unique<PatternSource>(format,p.key,0),std::make_unique<PatternSource>(format,p.key,1)};
        const auto source_bound=geometry(p.slot.format).source_workspace_bytes;
        require(prepared[0]->working_bytes()<=source_bound&&prepared[1]->working_bytes()<=source_bound,
                "HF private pair exceeds public source workspace");
        p.counters.private_source_bytes=checked_add(prepared[0]->working_bytes(),prepared[1]->working_bytes());
        p.sources=std::move(prepared);
    }
    const std::array<PatternSource*,2> sources{p.sources[0].get(),p.sources[1].get()};
    PatternSource::render_bank(sources,origin,1.L,offsets.size(),output);
    constexpr long double tau=2*std::numbers::pi_v<long double>;
    const auto common=std::polar(1.,0.);
    const auto tap_rotation=std::polar(1.,0.);
    const auto difference=((1.L-1)*static_cast<long double>(first)-1*shift_seconds*p.config.sample_rate_hz)/p.config.sample_rate_hz;
    const auto cycles=std::remainder(static_cast<long double>(p.config.data_if_hz)*difference,1.L);
    const auto center_rotation=std::polar(1.,static_cast<double>(tau*cycles));
    for (auto& value:output) {
        value=value*common*tap_rotation;
        if (p.config.data_if_hz!=0) value*=center_rotation;
        value*=p.config.data_basis_gain;
        require(std::isfinite(value.real())&&std::isfinite(value.imag()),"HF native pair returned nonfinite output");
    }
    ++p.counters.bank_calls;p.counters.bank_direct_columns+=2;
    p.counters.bank_column_samples+=output.size();
    check_cancelled(stop);
}
HfPublicBasisPairReport HfPublicBasisPair::report() const {return impl_->counters;}

std::array<double,2> combine_hf_public_phase_evidence(const std::array<double,2>& old_family,
        const std::array<double,2>& new_family) {
    return {log_add(old_family[0],new_family[0])-std::numbers::ln2,
            log_add(old_family[1],new_family[1])-std::numbers::ln2};
}

HfPublicTimingFrontier select_hf_refinement_timing(std::span<const double> old_presence,
        std::span<const double> segmented_presence) {
    require(!old_presence.empty()&&old_presence.size()==segmented_presence.size()&&old_presence.size()<=maximum_timing_atoms,
            "HF refinement timing rank extent is invalid");
    const auto top=[](std::span<const double> scores) {
        std::vector<std::size_t> indices(scores.size());std::iota(indices.begin(),indices.end(),0);
        for (const auto score:scores) require(std::isfinite(score)||score==negative_infinity,"HF refinement timing rank is invalid");
        std::stable_sort(indices.begin(),indices.end(),[&](auto a,auto b){return scores[a]>scores[b];});
        indices.resize(std::min<std::size_t>(3,indices.size()));return indices;
    };
    HfPublicTimingFrontier result;
    result.old_top=top(old_presence);result.segmented_top=top(segmented_presence);
    result.selected=result.old_top;
    for (const auto i:result.segmented_top)
        if (std::ranges::find(result.selected,i)==result.selected.end()) result.selected.push_back(i);
    require(result.selected.size()<=maximum_refinement_timings,"HF refinement timing frontier exceeded fixed cap");
    return result;
}

HfPublicTimingSelection select_hf_public_timing(const ReceiverConfig& config,const ReceiverSlotPlan& slot,
        const FinePreparation& prepared,bool full_timing,bool close_small_arrival_grid) {
    return timing_subset(config,slot,prepared,full_timing,close_small_arrival_grid);
}

bool hf_public_search_supported(const ReceiverConfig& config,const ReceiverSlotPlan& slot,bool phase_refinement) {
    if (!public_geometry_supported(config,slot)) return false;
    try {
        // Support is fixed before observing a slot and is invariant across
        // worker/owner shapes. Charge the complete public timing grid here,
        // even though operational acquisition may retain a smaller subset.
        const auto count=timing_count(timing_grid(config,slot));
        (void)numerical_search_plan(config,slot,count,false,phase_refinement);
        return true;
    } catch (const PublicGeometryError&) {
        return false;
    } catch (const std::invalid_argument&) {
        // Public statistics/refinement planners use this type for deterministic
        // parameter/numerical-cap validation. Allocation/runtime errors are
        // deliberately not converted into unsupported geometry.
        return false;
    }
}

std::size_t hf_public_search_workspace(const ReceiverConfig& config,const ReceiverSlotPlan& slot,
                                      unsigned workers,bool include_affine,bool close_small_arrival_grid,bool phase_refinement) {
    const auto grid=timing_grid(config,slot);
    const auto count=timing_count(grid);
    const auto bounded_count=close_small_arrival_grid&&grid.arrival_intervals<=2?
        count:std::min(count,maximum_selected_timing_atoms);
    return search_plan(config,slot,workers,bounded_count,include_affine,phase_refinement).workspace;
}

HfPublicSearchResult score_hf_public_search(const ReceiverConfig& config,const ReceiverSlotPlan& slot,
        const datapump::Crypto& key,std::span<const Complex> iq,const FinePreparation& prepared,
        unsigned workers,std::size_t workspace_limit_bytes,bool full_timing,bool close_small_arrival_grid,bool include_affine,
        std::stop_token stop,ReceiverExecutor& executor,bool use_native_pair_render,bool phase_refinement) {
    const auto start=Clock::now();
    require(hf_public_search_supported(config,slot,phase_refinement)&&workers>0&&iq.size()==slot.stop_sample-slot.first_sample,
            "HF public search requires a complete supported public observation");
    check_cancelled(stop);
    auto selection=timing_subset(config,slot,prepared,full_timing,close_small_arrival_grid);
    const auto shape=search_plan(config,slot,workers,selection.coordinates.size(),include_affine,phase_refinement);
    const bool execute_refinement=shape.refinement.has_value();
    if (shape.workspace>workspace_limit_bytes)
        throw ReceiverWorkspaceError("HF public phase bank exceeds the admitted full stage workspace");
    for (std::size_t n=0;n<iq.size();++n) {
        if (n%4096==0) check_cancelled(stop);
        require(std::isfinite(iq[n].real())&&std::isfinite(iq[n].imag()),"HF public search received nonfinite IQ");
    }
    HfPublicSearchResult result;
    result.plan=shape.statistics;result.public_timing_atoms=shape.full_count;result.block_rows=shape.block_rows;
    result.refinement_enabled=phase_refinement;result.refinement_plan=shape.refinement;
    result.refinement_executed=execute_refinement;result.refinement_unavailable_reason=shape.refinement_unavailable_reason;
    result.refinement_work_bound=shape.refinement_work_bound;
    result.retained_statistics_workspace_bytes=shape.retained_statistics;
    result.workspace_bytes=shape.workspace;result.basis_workspace_bytes=shape.basis_workspace;
    result.basis_pair_workspace_bytes_per_lane=shape.pair_workspace;
    result.metadata_reserve_bytes=shape.metadata;result.stage_workers=shape.active;
    result.phase_error_bound_radians=shape.phase_error;result.timing=std::move(selection.coordinates);
    result.canonical_selected_timing_atoms=selection.canonical_selected_timing_atoms;
    result.arrival_grid_closed=selection.arrival_grid_closed;
    result.evidence.resize(result.timing.size());
    for (auto& pair:result.bit_log_e) pair.fill(negative_infinity);
    MappedBasis basis(config,slot,key,shape.tile,shape.active);
    // This immutable preparation depends only on the fixed public phase/CFO
    // geometry. Every timing lane and both bits borrow its same phase table
    // and FFT twiddles; each evaluator owns only its private FFT buffer.
    const HfPreparedPublicStatistics statistics(result.plan,stop);
    result.statistics_preparation_seconds=statistics.preparation_wall_seconds();
    result.statistics_phase_values_prepared=statistics.prepared_phase_values();
    result.statistics_shared_workspace_bytes=statistics.shared_workspace_bytes();
    require(result.statistics_shared_workspace_bytes<=result.plan.shared_workspace_bytes,
            "HF public shared statistics allocation exceeds its quote");
    const std::array<Tap,1> paths{{{0,0}}};
    const auto blocks=result.plan.config.block_count;
    struct RetainedStatistics {std::array<std::vector<Complex>,2> q;std::array<std::vector<double>,2> energy;};
    std::vector<RetainedStatistics> retained(execute_refinement?result.timing.size():0);
    std::vector<double> rank_seconds(execute_refinement?result.timing.size():0);
    if (execute_refinement) result.refinement_ranks.resize(result.timing.size());
    executor.parallel(slot.index,result.timing.size(),shape.active,stop,[&](unsigned,std::size_t index,std::stop_token leaf_stop) {
        auto& evidence=result.evidence[index];
        std::array<std::vector<Complex>,2> private_q;
        auto& q=execute_refinement?retained[index].q:private_q;
        for (auto& bit:q) bit.resize(blocks);
        if (execute_refinement) for (auto& bit:retained[index].energy) bit.resize(blocks);
        std::vector<Complex> rendered(2*shape.tile);
        std::vector<double> offsets(shape.tile);
        std::iota(offsets.begin(),offsets.end(),0.);
        std::array<long double,2> energy{};
        const auto& coordinate=result.timing[index];
        const double scale=1+coordinate.clock_ppm*1e-6;
        const double arrival=static_cast<double>(slot.window_capture_lead_seconds+
            coordinate.arrival_seconds+slot.source_start_seconds/scale);
        const MappedBasisRow row{coordinate.epoch_offset_seconds,arrival,
            -config.data_if_hz*(scale-1),coordinate.clock_ppm};
        const auto basis_start=Clock::now();
        HfPublicBasisPair pair(config,slot,key,row,use_native_pair_render);
        for (std::size_t first=0;first<iq.size();) {
            check_cancelled(leaf_stop);
            const auto count=std::min(shape.tile,iq.size()-first);
            pair.render(basis,paths,first,{offsets.data(),count},{rendered.data(),2*count},leaf_stop);
            for (std::size_t i=0;i<count;++i)
                for (std::size_t bit=0;bit<2;++bit) {
                    const auto x=rendered[bit*count+i];
                    q[bit][(first+i)/shape.block_rows]+=std::conj(x)*iq[first+i];
                    const auto norm=std::norm(x);
                    energy[bit]+=norm;
                    if (execute_refinement) retained[index].energy[bit][(first+i)/shape.block_rows]+=norm;
                }
            first+=count;
        }
        evidence.basis_seconds=elapsed(basis_start);
        evidence.basis_pair=pair.report();
        evidence.energy={static_cast<double>(energy[0]),static_cast<double>(energy[1])};
        evidence.statistics=hf_public_statistics(statistics,{q[0],q[1]},evidence.energy,leaf_stop);
        if (execute_refinement) {
            const auto began=Clock::now();
            result.refinement_ranks[index]=hf_phase_timing_rank(*result.refinement_plan,statistics,
                {q[0],q[1]},{retained[index].energy[0],retained[index].energy[1]},leaf_stop);
            rank_seconds[index]=elapsed(began);
        }
    });
    const double prior=std::log(static_cast<double>(shape.full_count));
    for (const auto& evidence:result.evidence) {
        result.statistics_actual_workspace_peak_bytes=std::max(result.statistics_actual_workspace_peak_bytes,
            evidence.statistics.actual_lane_workspace_bytes);
        result.statistics_summed_worker_wall_seconds+=evidence.statistics.evaluator_wall_seconds;
        result.basis_summed_worker_wall_seconds+=evidence.basis_seconds;
        result.basis_pair.bank_calls+=evidence.basis_pair.bank_calls;
        result.basis_pair.bank_direct_columns+=evidence.basis_pair.bank_direct_columns;
        result.basis_pair.bank_column_samples+=evidence.basis_pair.bank_column_samples;
        result.basis_pair.fallback_calls+=evidence.basis_pair.fallback_calls;
        result.basis_pair.coordinate_guard_fallbacks+=evidence.basis_pair.coordinate_guard_fallbacks;
        result.basis_pair.private_source_bytes=std::max(result.basis_pair.private_source_bytes,evidence.basis_pair.private_source_bytes);
        if (!evidence.basis_pair.native_fallback_reason.empty()) {
            if (result.basis_pair.native_fallback_reason.empty()) result.basis_pair.native_fallback_reason=evidence.basis_pair.native_fallback_reason;
            else if (result.basis_pair.native_fallback_reason!=evidence.basis_pair.native_fallback_reason)
                result.basis_pair.native_fallback_reason="multiple_bounded_fallback_reasons";
        }
        for (std::size_t f=0;f<3;++f) if (evidence.statistics.families[f].evaluated)
            for (std::size_t bit=0;bit<2;++bit)
                result.bit_log_e[f][bit]=log_add(result.bit_log_e[f][bit],
                    evidence.statistics.families[f].hypotheses[bit].log_likelihood_ratio-prior);
    }
    result.combined_phase_bit_log_e=result.bit_log_e[2];
    if (execute_refinement) {
        const auto second_start=Clock::now();
        std::vector<double> old_ranks(result.timing.size()),segmented_ranks(result.timing.size());
        for (std::size_t i=0;i<result.timing.size();++i) {
            const auto& h=result.evidence[i].statistics.families[2].hypotheses;
            old_ranks[i]=log_add(h[0].log_likelihood_ratio,h[1].log_likelihood_ratio)-std::numbers::ln2;
            const auto& rank=result.refinement_ranks[i];
            segmented_ranks[i]=log_add(rank.bit_scores[0],rank.bit_scores[1])-std::numbers::ln2;
            add_work(result.refinement_work,rank.work);
            result.refinement_summed_worker_wall_seconds+=rank_seconds[i];
        }
        result.refinement_frontier=select_hf_refinement_timing(old_ranks,segmented_ranks);
        result.refinement_evidence.resize(result.refinement_frontier.selected.size());
        std::vector<double> refine_seconds(result.refinement_evidence.size());
        executor.parallel(slot.index,result.refinement_evidence.size(),
            static_cast<unsigned>(std::min<std::size_t>(workers,result.refinement_evidence.size())),stop,
            [&](unsigned,std::size_t item,std::stop_token leaf_stop) {
                const auto began=Clock::now();
                const auto index=result.refinement_frontier.selected[item];
                const auto& saved=retained[index];
                auto& entry=result.refinement_evidence[item];entry.timing_index=index;
                entry.report=hf_phase_refinement(*result.refinement_plan,statistics,
                    {saved.q[0],saved.q[1]},{saved.energy[0],saved.energy[1]},result.evidence[index].energy,leaf_stop);
                refine_seconds[item]=elapsed(began);
            });
        for (std::size_t i=0;i<result.refinement_evidence.size();++i) {
            const auto& report=result.refinement_evidence[i].report;
            add_work(result.refinement_work,report.work);
            result.refinement_summed_worker_wall_seconds+=refine_seconds[i];
            for (std::size_t bit=0;bit<2;++bit)
                result.new_phase_bit_log_e[bit]=log_add(result.new_phase_bit_log_e[bit],report.bit_log_e[bit]-prior);
        }
        result.refinement_wall_seconds=elapsed(second_start);
        const auto& actual=result.refinement_work;const auto& bound=result.refinement_work_bound;
        require(actual.fft_row_stages<=bound.fft_row_stages&&actual.spectrum_cells<=bound.spectrum_cells&&
            actual.table_lookups<=bound.table_lookups&&actual.direct_sweeps<=bound.direct_sweeps&&
            actual.direct_rows<=bound.direct_rows&&actual.phase_rotations<=bound.phase_rotations&&
            actual.trigonometric_evaluations<=bound.trigonometric_evaluations&&actual.likelihood_cells<=bound.likelihood_cells,
            "HF refinement work exceeded its predeclared public quote");
    }
    if (phase_refinement)
        result.combined_phase_bit_log_e=combine_hf_public_phase_evidence(result.bit_log_e[2],result.new_phase_bit_log_e);
    result.cache=basis.report();result.search_seconds=elapsed(start);
    return result;
}

Json diagnose_hf_public_search(const ReceiverConfig& config,const ReceiverSlotPlan& slot,
        const datapump::Crypto& key,std::span<const Complex> iq,unsigned workers,
        std::size_t total_memory_limit_bytes,bool full_timing,std::stop_token stop,HfPublicSearchResult* search_result,bool phase_refinement) {
    const auto start=Clock::now();
    const auto cpu_start=detail::stream_process_cpu_seconds();
    require(hf_public_search_supported(config,slot,phase_refinement)&&workers>0&&iq.size()==slot.stop_sample-slot.first_sample,
            "HF public search requires a complete Helper-off, CPU, single-tap white-noise observation");
    const auto footprint=std::max(datapump::execution::process_footprint_bytes(),iq.size_bytes());
    const auto stacks=checked_multiply(workers-1,datapump::execution::worker_stack_bytes());
    // Persistent coarse/preparation traces live alongside the shared scorer's
    // separately charged structured results and eventual JSON conversion.
    constexpr std::size_t coarse_metadata=65536;
    const auto base=checked_add(checked_add(footprint,stacks),coarse_metadata);
    if (base>=total_memory_limit_bytes)
        throw ReceiverWorkspaceError("HF public search input, metadata and worker stacks exhaust memory");
    const auto scratch=total_memory_limit_bytes-base;
    ReceiverExecutor executor(workers);
    const FrequencyInterval prior{-config.frequency_bound_hz,config.frequency_bound_hz};
    const auto coarse_quote=coarse_work_plan(config,slot,{&prior,1},workers);
    if (coarse_quote.workspace_reserved_bytes>scratch)
        throw ReceiverWorkspaceError("HF public coarse search exceeds the admitted workspace");
    const auto coarse_start=Clock::now();
    const auto coarse=score_coarse(config,slot,key,iq,{&prior,1},workers,scratch,stop,&executor);
    const double coarse_seconds=elapsed(coarse_start);
    const auto prepared=prepare_fine_coordinates(config,slot,coarse);
    auto search=score_hf_public_search(config,slot,key,iq,prepared,workers,scratch,full_timing,false,!phase_refinement,stop,executor,true,phase_refinement);
    const auto& plan=search.plan;
    Json::Array coordinates,proposals,families;
    for (std::size_t index=0;index<search.timing.size();++index) {
        auto item=coordinate_json(search.timing[index]);
        const auto& evidence=search.evidence[index];
        item["template_energy"]=Json::Array{evidence.energy[0],evidence.energy[1]};
        item["basis_wall_seconds"]=evidence.basis_seconds;
        item["basis_bank_calls"]=evidence.basis_pair.bank_calls;
        item["basis_fallback_calls"]=evidence.basis_pair.fallback_calls;
        item["statistics_wall_seconds"]=evidence.statistics.evaluator_wall_seconds;
        Json::Array family_evidence;
        for (std::size_t f=0;f<3;++f) {
            if (!plan.family_model_counts[f]) {family_evidence.emplace_back();continue;}
            Json::Array bits;
            for (std::size_t bit=0;bit<2;++bit) {
                const auto& h=evidence.statistics.families[f].hypotheses[bit];
                bits.push_back(object({{"log_e_before_timing_prior",h.log_likelihood_ratio},
                    {"maximum_log_lr",h.maximum_log_likelihood_ratio},{"maximum_frequency_hz",h.maximum_frequency_hz},
                    {"maximum_model_index",h.maximum_model_index}}));
            }
            family_evidence.push_back(std::move(bits));
        }
        item["family_bit_evidence"]=std::move(family_evidence);coordinates.push_back(std::move(item));
    }
    constexpr std::array names{"constant_frequency","linear_frequency_drift","sinusoidal_phase"};
    for (std::size_t f=0;f<3;++f) {
        if (!plan.family_model_counts[f]) {families.push_back(object({{"name",names[f]},{"evaluated",false}}));continue;}
        const auto& pair=search.bit_log_e[f];
        const double presence=log_add(pair[0],pair[1])-std::numbers::ln2;
        families.push_back(object({{"name",names[f]},{"phase_models",plan.family_model_counts[f]},
            {"bit_log_e",Json::Array{pair[0],pair[1]}},{"presence_log_e",presence},
            {"favored_bit",pair[1]>pair[0]?1:0},{"bit_log_separation",std::abs(pair[1]-pair[0])},
            {"passes_presence_reference",presence>=receiver_operational_threshold()},
            {"passes_bit_separation_reference",std::abs(pair[1]-pair[0])>=config.time_varying_minimum_bit_log_separation},
            {"logical_fft_row_stages",plan.family_fft_row_stages[f]*search.timing.size()}}));
    }
    for (std::size_t i=0;i<coarse.proposal_count;++i) {
        auto item=coordinate_json(coarse.proposals[i].coordinate);
        item["frequency_hz"]=coarse.proposals[i].coordinate.frequency_hz;
        item["score"]=coarse.proposals[i].score;item["bit"]=coarse.proposals[i].bit;
        proposals.push_back(std::move(item));
    }
    const auto cpu_end=detail::stream_process_cpu_seconds();
    auto report=summary_json(search);
    report["component_complete"]=true;report["receiver_executed"]=false;report["completion_evaluated"]=false;
    report["capacity_qualified"]=false;report["truth_information_passed_to_search"]=false;report["slot_index"]=slot.index;
    report["scope"]="shadow fixed-prior raw-IQ likelihoods; canonical line/quiet/codec policy is not executed";
    report["null_model"]="known-variance proper-complex IID Gaussian; one CN(0,1) gain per alternative";
    report["timing_selection"]=full_timing?"full_public_grid":"native_coarse_selected_union";
    report["affine_slope_bound_hz_per_second"]=comparison_slope_bound;
    report["affine_slope_step_hz_per_second"]=plan.affine_slope_step_hz_per_second;
    report["affine_nearest_grid_phase_error_bound_radians"]=plan.affine_nearest_grid_phase_error_bound_radians;
    report["gain_prior_variance"]=1.;report["presence_reference_threshold"]=receiver_operational_threshold();
    report["bit_separation_reference"]=config.time_varying_minimum_bit_log_separation;
    report["families"]=std::move(families);report["coordinates"]=std::move(coordinates);report["coarse_proposals"]=std::move(proposals);
    report["coarse_seconds"]=coarse_seconds;report["coarse_search_complete"]=coarse.work.completed_tiles==coarse.work.plan.total_tiles;
    report["coarse_tiles"]=coarse.work.completed_tiles;report["coarse_frequency_points"]=coarse.frequency_points;
    report["coarse_arrival_points"]=coarse.arrival_points;report["coarse_workspaces"]=coarse.workspaces;
    report["wall_seconds"]=elapsed(start);report["process_cpu_seconds"]=cpu_start&&cpu_end?Json(*cpu_end-*cpu_start):Json{};
    report["workers"]=workers;report["executor_peak_workers"]=executor.peak_workers();report["process_limit_bytes"]=total_memory_limit_bytes;
    report["input_process_footprint_bytes"]=footprint;report["worker_stack_reserve_bytes"]=stacks;
    report["coarse_metadata_reserve_bytes"]=coarse_metadata;report["scratch_limit_bytes"]=scratch;
    report["coarse_workspace_bytes"]=coarse_quote.workspace_reserved_bytes;
    // Output only: every public search decision and report field is complete
    // before evaluator ownership is transferred. No second allocation/copy.
    if (search_result) *search_result=std::move(search);
    return report;
}

struct HfPublicPhaseReceiver::Impl {
    struct RefinementSummary {
        bool executed=false;
        std::string unavailable_reason;
        std::array<double,2> old_bits{},new_bits{};
        std::size_t shape_count=0,nu_count=0,selected_count=0,old_count=0,segmented_count=0,retained_bytes=0;
        std::size_t segment_count=0,segment_rows=0,segment_fft_rows=0,coarse_nu_count=0,coarse_frequency_radius=0;
        std::size_t intermediate_group_rows=0,intermediate_group_count=0;
        double nu_step=0,retained_prior_mass=0,seconds=0,summed_worker_seconds=0;
        double segment_frequency_step=0,coarse_frequency_step=0;
        std::array<std::size_t,6> selected{},selected_shapes{};
        std::array<std::size_t,3> old_top{},segmented_top{};
        std::array<std::array<double,2>,6> ranks{};
        HfPhaseRefinementWork work,work_bound;
    };
    struct SlotSummary {
        std::uint64_t index=0;
        std::array<double,2> bit_log_e{};
        RefinementSummary refinement;
        std::size_t full_count=0, canonical_count=0, evaluated_count=0, frequency_cells=0, block_rows=0, blocks=0, fft_rows=0;
        bool arrival_grid_closed=false;
        HfPublicBasisPairReport basis_pair;
        std::size_t pair_workspace=0;
        std::uint64_t direct_columns=0,interpolated_columns=0;
        std::size_t workspace=0, statistics_workspace=0, statistics_shared_workspace=0,
            statistics_shared_quote=0, statistics_lane_quote=0, basis_cache_peak=0, metadata=0;
        std::uint64_t fft_work=0, phase_work=0, likelihood_cells=0, prepared_phase_values=0;
        unsigned workers=0, stage_workers=0;
        double frequency_step=0, actual_bound=0, phase_error=0, seconds=0;
        double preparation_seconds=0, statistics_summed_worker_seconds=0, basis_summed_worker_seconds=0;
    };
    explicit Impl(std::size_t maximum,bool refine):slots(maximum),refinement_enabled(refine) {}
    std::mutex mutex;
    std::vector<std::optional<SlotSummary>> slots;
    bool refinement_enabled=false;
    // Saturation keeps telemetry bounded and cannot reject receiver evidence.
    std::uint64_t overflow_reports=0, duplicate_reports=0;
};

HfPublicPhaseReceiver::HfPublicPhaseReceiver(std::size_t maximum_slot_reports,bool phase_refinement) {
    require(maximum_slot_reports<=65536,"HF phase receiver needs a bounded telemetry slot count");
    // Persistent summaries and their final JSON are covered by the caller's
    // evaluator reservation (>=16KiB off, >=64KiB refined), not DSP scratch.
    static_assert(sizeof(Impl::SlotSummary)<4096);
    impl_=std::make_shared<Impl>(maximum_slot_reports,phase_refinement);
}

SlotEvidenceSupplement HfPublicPhaseReceiver::supplement() const {
    SlotEvidenceSupplement result;
    result.fixed_weight=supplement_weight;
    result.supported=[refine=impl_->refinement_enabled](const ReceiverConfig& c,const ReceiverSlotPlan& s) {
        return hf_public_search_supported(c,s,refine);
    };
    result.workspace=[refine=impl_->refinement_enabled](const ReceiverConfig& c,const ReceiverSlotPlan& s,unsigned workers) {
        return hf_public_search_workspace(c,s,workers,false,true,refine);
    };
    result.score=[owner=impl_](const ReceiverConfig& c,const ReceiverSlotPlan& s,const datapump::Crypto& key,
            std::span<const Complex> iq,const FinePreparation& prepared,unsigned workers,std::size_t workspace,
            std::stop_token stop,ReceiverExecutor& executor) {
        const auto search=score_hf_public_search(c,s,key,iq,prepared,workers,workspace,false,true,false,stop,executor,true,owner->refinement_enabled);
        const auto& p=search.plan;
        Impl::SlotSummary summary;
        summary.index=s.index;summary.bit_log_e=search.combined_phase_bit_log_e;summary.full_count=search.public_timing_atoms;
        summary.canonical_count=search.canonical_selected_timing_atoms;summary.arrival_grid_closed=search.arrival_grid_closed;
        summary.evaluated_count=search.timing.size();summary.frequency_cells=p.frequency_count;summary.block_rows=search.block_rows;
        summary.blocks=p.config.block_count;summary.fft_rows=p.fft_rows;summary.workspace=search.workspace_bytes;
        summary.basis_pair=search.basis_pair;summary.pair_workspace=search.basis_pair_workspace_bytes_per_lane;
        summary.direct_columns=search.cache.direct_rows+search.basis_pair.bank_direct_columns;
        summary.interpolated_columns=search.cache.interpolated_rows;
        summary.statistics_workspace=search.statistics_actual_workspace_peak_bytes;
        summary.statistics_shared_workspace=search.statistics_shared_workspace_bytes;
        summary.statistics_shared_quote=p.shared_workspace_bytes;summary.statistics_lane_quote=p.lane_workspace_bytes;
        summary.prepared_phase_values=search.statistics_phase_values_prepared;
        summary.preparation_seconds=search.statistics_preparation_seconds;
        summary.statistics_summed_worker_seconds=search.statistics_summed_worker_wall_seconds;
        summary.basis_summed_worker_seconds=search.basis_summed_worker_wall_seconds;
        summary.basis_cache_peak=search.cache.peak_cache_bytes;
        summary.metadata=search.metadata_reserve_bytes;summary.fft_work=p.actual_fft_row_stages*search.timing.size();
        summary.phase_work=p.phase_complex_multiplications*search.timing.size();summary.likelihood_cells=p.likelihood_cells*search.timing.size();
        summary.workers=workers;summary.stage_workers=search.stage_workers;summary.frequency_step=p.frequency_step_hz;
        summary.actual_bound=p.actual_frequency_bound_hz;summary.phase_error=search.phase_error_bound_radians;summary.seconds=search.search_seconds;
        if (search.refinement_enabled) {
            auto& r=summary.refinement;
            r.executed=search.refinement_executed;r.unavailable_reason=search.refinement_unavailable_reason;
            r.old_bits=search.bit_log_e[2];r.new_bits=search.new_phase_bit_log_e;
            r.work=search.refinement_work;r.work_bound=search.refinement_work_bound;
        }
        if (search.refinement_executed) {
            auto& r=summary.refinement;const auto& plan=*search.refinement_plan;
            r.shape_count=plan.shape_count;r.nu_count=plan.nu_count;r.nu_step=plan.nu_step_hz;
            r.segment_count=plan.segment_count;r.segment_rows=plan.segment_rows;r.segment_fft_rows=plan.segment_fft_rows;
            r.intermediate_group_rows=plan.intermediate_group_rows;r.intermediate_group_count=plan.intermediate_group_count;
            r.segment_frequency_step=plan.segment_frequency_step_hz;r.coarse_nu_count=plan.coarse_nu_count;
            r.coarse_frequency_step=plan.coarse_frequency_step_hz;r.coarse_frequency_radius=plan.coarse_frequency_radius;
            r.retained_bytes=search.retained_statistics_workspace_bytes;
            r.seconds=search.refinement_wall_seconds;r.summed_worker_seconds=search.refinement_summed_worker_wall_seconds;
            r.selected_count=search.refinement_frontier.selected.size();r.old_count=search.refinement_frontier.old_top.size();
            r.segmented_count=search.refinement_frontier.segmented_top.size();
            std::copy(search.refinement_frontier.selected.begin(),search.refinement_frontier.selected.end(),r.selected.begin());
            std::copy(search.refinement_frontier.old_top.begin(),search.refinement_frontier.old_top.end(),r.old_top.begin());
            std::copy(search.refinement_frontier.segmented_top.begin(),search.refinement_frontier.segmented_top.end(),r.segmented_top.begin());
            for (std::size_t k=0;k<r.selected_count;++k) {
                const auto index=r.selected[k];const auto& old=search.evidence[index].statistics.families[2].hypotheses;
                const auto& rank=search.refinement_ranks[index];
                r.ranks[k]={log_add(old[0].log_likelihood_ratio,old[1].log_likelihood_ratio)-std::numbers::ln2,
                    log_add(rank.bit_scores[0],rank.bit_scores[1])-std::numbers::ln2};
                r.selected_shapes[k]=search.refinement_evidence[k].report.selected_count;
                r.retained_prior_mass+=search.refinement_evidence[k].report.retained_shape_prior_mass/search.public_timing_atoms;
            }
        }
        {
            const std::lock_guard lock(owner->mutex);
            const auto increment=[](std::uint64_t& counter) {if (counter<UINT64_MAX) ++counter;};
            // The evaluator's report horizon may depend on simulation truth;
            // it must never choose whether or how the receiver scores IQ.
            if (s.index>=owner->slots.size()) increment(owner->overflow_reports);
            else {
                if (owner->slots[s.index]) increment(owner->duplicate_reports);
                owner->slots[s.index]=summary;
            }
        }
        return summary.bit_log_e;
    };
    return result;
}

Json HfPublicPhaseReceiver::report() const {
    Json::Array slots;
    const std::lock_guard lock(impl_->mutex);
    for (const auto& entry:impl_->slots) if (entry) {
        const auto& s=*entry;
        Json refinement=object({{"enabled",false}});
        if (impl_->refinement_enabled&&!s.refinement.executed)
            refinement=unavailable_refinement_json(s.refinement.old_bits,s.bit_log_e,
                s.refinement.unavailable_reason,s.refinement.work_bound);
        if (impl_->refinement_enabled&&s.refinement.executed) {
            const auto& r=s.refinement;Json::Array ranks,counts;
            for (std::size_t k=0;k<r.selected_count;++k) {
                ranks.push_back(object({{"timing_index",r.selected[k]},{"old_presence_rank",r.ranks[k][0]},
                    {"segmented_presence_rank",r.ranks[k][1]}}));counts.emplace_back(r.selected_shapes[k]);
            }
            refinement=object({{"enabled",true},{"requested",true},{"executed",true},{"unavailable_reason",""},
                {"old_family_weight",.5},{"new_family_weight",.5},
                {"old_bit_log_e",Json::Array{r.old_bits[0],r.old_bits[1]}},{"new_bit_log_e",Json::Array{r.new_bits[0],r.new_bits[1]}},
                {"combined_bit_log_e",Json::Array{s.bit_log_e[0],s.bit_log_e[1]}},
                {"shape_count",r.shape_count},{"nu_count",r.nu_count},{"nu_step_hz",r.nu_step},
                {"segment_count",r.segment_count},{"segment_rows",r.segment_rows},{"segment_fft_rows",r.segment_fft_rows},
                {"intermediate_group_rows",r.intermediate_group_rows},{"intermediate_group_count",r.intermediate_group_count},
                {"intermediate_iteration_limit",r.intermediate_group_count?HfPhaseRefinementPlan::intermediate_iterations:0},
                {"coherent_iteration_limit",HfPhaseRefinementPlan::maximum_iterations-
                    (r.intermediate_group_count?HfPhaseRefinementPlan::intermediate_iterations:0)},
                {"segment_frequency_step_hz",r.segment_frequency_step},
                {"coarse_nu_count",r.coarse_nu_count},{"coarse_frequency_step_hz",r.coarse_frequency_step},
                {"coarse_frequency_radius",r.coarse_frequency_radius},
                {"public_timing_atoms",s.full_count},{"frequency_cells",s.frequency_cells},
                {"selected_timing_indices",indices_json({r.selected.data(),r.selected_count})},
                {"old_top_timing_indices",indices_json({r.old_top.data(),r.old_count})},
                {"segmented_top_timing_indices",indices_json({r.segmented_top.data(),r.segmented_count})},
                {"timing_ranks",std::move(ranks)},{"selected_shape_counts",std::move(counts)},
                {"timing_rank_scope","selected union only; complete proposal traces belong to shadow diagnostics"},
                {"retained_new_prior_mass",r.retained_prior_mass},{"retained_statistics_workspace_bytes",r.retained_bytes},
                {"work",work_json(r.work)},{"work_bound",work_json(r.work_bound)},
                {"second_stage_wall_seconds",r.seconds},{"summed_worker_wall_seconds",r.summed_worker_seconds}});
        }
        slots.push_back(object({{ "slot_index",s.index},{"component_complete",true},{"executed",true},
            {"phase_refinement",std::move(refinement)},
            {"bit_log_e",Json::Array{s.bit_log_e[0],s.bit_log_e[1]}},
            {"presence_log_e_before_branch_weight",log_add(s.bit_log_e[0],s.bit_log_e[1])-std::numbers::ln2},
            {"public_timing_atoms",s.full_count},{"evaluated_timing_atoms",s.evaluated_count},
            {"canonical_selected_timing_atoms",s.canonical_count},{"arrival_grid_closed",s.arrival_grid_closed},
            {"retained_timing_prior_mass",static_cast<double>(s.evaluated_count)/s.full_count},
            {"frequency_cells",s.frequency_cells},{"phase_models",97},{"frequency_step_hz",s.frequency_step},
            {"actual_frequency_bound_hz",s.actual_bound},{"block_rows",s.block_rows},{"blocks",s.blocks},{"fft_rows",s.fft_rows},
            {"effective_template_phase_error_bound_radians",s.phase_error},
            {"effective_template_phase_error_bound_scope","fixed97 and radius16 Cartesian refinement; excludes phase-grid/model mismatch"},
            {"search_workspace_bytes",s.workspace},{"statistics_actual_workspace_peak_bytes_per_lane",s.statistics_workspace},
            {"statistics_shared_workspace_bytes",s.statistics_shared_workspace},
            {"statistics_shared_workspace_quote_bytes",s.statistics_shared_quote},
            {"statistics_lane_workspace_quote_bytes",s.statistics_lane_quote},
            {"statistics_preparation_seconds",s.preparation_seconds},{"statistics_phase_values_prepared",s.prepared_phase_values},
            {"statistics_summed_worker_wall_seconds",s.statistics_summed_worker_seconds},
            {"basis_summed_worker_wall_seconds",s.basis_summed_worker_seconds},
            {"metadata_reserve_bytes",s.metadata},{"basis_cache_peak_bytes",s.basis_cache_peak},
            {"basis_bank_calls",s.basis_pair.bank_calls},{"basis_bank_direct_columns",s.basis_pair.bank_direct_columns},
            {"basis_bank_column_samples",s.basis_pair.bank_column_samples},{"basis_fallback_calls",s.basis_pair.fallback_calls},
            {"basis_direct_columns",s.direct_columns},{"basis_interpolated_columns",s.interpolated_columns},
            {"basis_coordinate_guard_fallbacks",s.basis_pair.coordinate_guard_fallbacks},
            {"basis_pair_native_fallback_reason",s.basis_pair.native_fallback_reason},
            {"basis_pair_workspace_bytes_per_lane",s.pair_workspace},
            {"actual_fft_row_stages",s.fft_work},
            {"total_fft_row_stages_including_refinement",s.fft_work+s.refinement.work.fft_row_stages},
            {"phase_complex_multiplications",s.phase_work},{"likelihood_cells",s.likelihood_cells},
            {"workers",s.workers},{"stage_workers",s.stage_workers},{"search_seconds",s.seconds}}));
    }
    const auto count=slots.size();
    return object({{"experimental",true},{"fixed_weight",supplement_weight},
        {"refinement_enabled",impl_->refinement_enabled},
        {"model_family",impl_->refinement_enabled?"equal_prior_fixed97_and_cartesian_refinement":"sinusoidal_phase_97"},
        {"assumed_public_taps",1},{"channel_model_scope","actual channel may be mismatched; no calibrated multipath-recovery claim"},
        {"truth_information_passed_to_search",false},{"selected_timing_renormalized",false},
        {"maximum_slot_reports",impl_->slots.size()},{"executed_slots",count},{"slots",std::move(slots)},
        {"telemetry_overflow_reports",impl_->overflow_reports},{"telemetry_duplicate_reports",impl_->duplicate_reports},
        {"persistent_summary_reserved_bytes",impl_->slots.capacity()*sizeof(std::optional<Impl::SlotSummary>)},
        {"unlisted_slot_semantics","no stored callback report; overflow is counted separately, otherwise no callback for unsupported geometry, early canonical DATA, invalidity or cancellation"},
        {"admission_scope","raw contribution supplied to canonical classification; a retained coherent line suppresses it without restoring prior mass"},
        {"public_search_arithmetic",arithmetic},{"maximum_total_fft_row_stages",maximum_total_fft_row_stages},
        {"summed_worker_wall_times_scope","overlapping per-timing elapsed intervals; not CPU time or complete service time"}});
}
} // namespace appraisal
