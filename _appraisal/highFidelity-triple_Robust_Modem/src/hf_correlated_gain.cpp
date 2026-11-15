#include "appraisal/hf_correlated_gain.hpp"

#include "datapump/execution_resources.hpp"
#include "search_fft.hpp"
#include "triad_receiver_basis.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numbers>
#include <numeric>

namespace appraisal {
namespace {
using namespace datapump::triad::v3;
using namespace receiver_detail;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
namespace fft = datapump::modem::detail::search_fft;
using Plan = HfCorrelatedGainPlan;
using Clock = std::chrono::steady_clock;
constexpr double pi = std::numbers::pi;
constexpr double absent = -std::numeric_limits<double>::infinity();
constexpr double charged_supplement_weight = .125;

void require(bool condition, const char* reason) {
    if (!condition) throw datapump::Error(reason);
}

double log_add(double a, double b) {
    if (a == absent || b == absent) return std::max(a, b);
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}

double frequency(std::size_t bin) {
    return -Plan::frequency_bound_hz + bin * Plan::frequency_step_hz;
}

struct Trajectory {
    double covariance = 0;
    std::array<Complex, Plan::frequency_count> means{};
    std::array<double, Plan::frequency_count> scores{};
};

struct Lane {
    std::array<double, Plan::group_rows> offsets;
    std::array<Complex, 2 * Plan::group_rows> pair;
    std::array<Complex, Plan::fft_rows> fft_scratch;
    std::array<Complex, Plan::frequency_count> normalized_q;
    std::array<std::array<Trajectory, Plan::laws>, 2> state;

    Lane() { std::iota(offsets.begin(), offsets.end(), 0.); }
};

struct TimingEvidence {
    std::array<double, 2> mixture{absent, absent}, maximum{absent, absent};
    std::array<std::array<double, 2>, Plan::laws> per_law{};
    HfPublicBasisPairReport rendering;
    std::uint64_t basis_products = 0, fft_transforms = 0, innovations = 0;
    double basis_seconds = 0, projection_seconds = 0, innovation_seconds = 0;
};

struct GainLaw { double power, rho; };

std::array<GainLaw, Plan::laws> gain_laws() {
    std::array<GainLaw, Plan::laws> laws;
    std::size_t i = 0;
    for (const double duration : Plan::coherence_seconds)
        for (const double power : Plan::gain_powers)
            laws[i++] = {power, std::exp(-.5 / duration)};
    return laws;
}

TimingEvidence score_timing(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const datapump::Crypto& key, std::span<const Complex> iq, const FineCoordinate& timing,
        MappedBasis& basis, const HfGainFrequencyProjection& projection,
        const std::array<GainLaw, Plan::laws>& laws, Lane& lane, std::stop_token stop) {
    const auto arrival = static_cast<double>(slot.window_capture_lead_seconds +
        timing.arrival_seconds + slot.source_start_seconds);
    const MappedBasisRow row{timing.epoch_offset_seconds, arrival, 0, 0};
    HfPublicBasisPair renderer(config, slot, key, row);
    constexpr std::array<Tap, 1> taps{{{0, 0}}};
    const double inverse_noise = 1 / config.noise_variance;
    TimingEvidence result;
    lane.state = {};
    for (auto& bit : lane.state)
        for (std::size_t law = 0; law < laws.size(); ++law)
            bit[law].covariance = laws[law].power;

    for (std::size_t first = 0; first < iq.size(); first += Plan::group_rows) {
        check_cancelled(stop);
        const auto rows = std::min(Plan::group_rows, iq.size() - first);
        const auto atoms = 1 + (rows - 1) / Plan::atom_rows;
        const auto basis_start = Clock::now();
        renderer.render(basis, taps, first, std::span(lane.offsets).first(rows),
                        std::span(lane.pair).first(2 * rows), stop);
        result.basis_seconds += std::chrono::duration<double>(Clock::now() - basis_start).count();
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto projection_start = Clock::now();
            std::array<Complex, Plan::atoms_per_group> q{};
            double energy = 0;
            for (std::size_t i = 0; i < rows; ++i) {
                const auto x = lane.pair[bit * rows + i];
                q[i / Plan::atom_rows] += std::conj(x) * iq[first + i];
                energy += std::norm(x);
            }
            const auto transformed = projection.project(first / Plan::group_rows,
                std::span(q).first(atoms), lane.fft_scratch, stop);
            for (std::size_t f = 0; f < transformed.size(); ++f)
                lane.normalized_q[f] = transformed[f] * inverse_noise;
            const auto innovation_start = Clock::now();
            result.projection_seconds += std::chrono::duration<double>(innovation_start - projection_start).count();
            const auto gram = energy * inverse_noise;
            for (std::size_t law = 0; law < laws.size(); ++law) {
                auto& state = lane.state[bit][law];
                const auto step = hf_gain_innovation(state.covariance, gram, laws[law].power, laws[law].rho);
                for (std::size_t f = 0; f < transformed.size(); ++f)
                    state.scores[f] += step.accumulate(state.means[f], lane.normalized_q[f]);
            }
            result.innovation_seconds += std::chrono::duration<double>(Clock::now() - innovation_start).count();
            result.basis_products += rows;
            ++result.fft_transforms;
            result.innovations += laws.size() * transformed.size();
        }
    }

    const double log_frequency_weight = -std::log(static_cast<double>(Plan::frequency_count - 1));
    const double log_law_weight = -std::log(static_cast<double>(Plan::laws));
    for (std::size_t bit = 0; bit < 2; ++bit) {
        for (std::size_t i = 0; i < Plan::laws; ++i) {
            const auto& scores = lane.state[bit][i].scores;
            double marginal = absent;
            for (std::size_t f = 0; f < scores.size(); ++f) {
                const double score = scores[f];
                require(std::isfinite(score), "HF correlated gain likelihood exceeds finite numeric limits");
                const double cell_weight = log_frequency_weight - (f == 0 || f + 1 == scores.size() ? std::numbers::ln2 : 0.);
                marginal = log_add(marginal, cell_weight + score);
                result.maximum[bit] = std::max(result.maximum[bit], score);
            }
            result.per_law[i][bit] = marginal;
            result.mixture[bit] = log_add(result.mixture[bit], marginal + log_law_weight);
        }
    }
    result.rendering = renderer.report();
    return result;
}

Json pair_json(const std::array<double, 2>& pair) { return Json::Array{pair[0], pair[1]}; }
} // namespace

HfCorrelatedGainPlan hf_correlated_gain_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    require(config.sample_rate_hz == Plan::sample_rate && slot.stop_sample > slot.first_sample &&
        config.clock_bound_ppm == 0 && config.endpoint_clock_bound_ppm == 0 &&
        config.frequency_bound_hz + config.time_varying_atmospheric_hz == Plan::frequency_bound_hz &&
        !config.helper_enabled && !config.bulk_backend && config.spread_delay_seconds == 0 &&
        config.spread_doppler_hz == 0 && config.noise_variance > 0 && std::isfinite(1 / config.noise_variance) &&
        std::isfinite(config.noise_variance) && std::isfinite(slot.arrival_lower_seconds) &&
        std::isfinite(slot.arrival_upper_seconds) && slot.arrival_upper_seconds >= slot.arrival_lower_seconds &&
        slot.arrival_upper_seconds - slot.arrival_lower_seconds <= .512,
        "HF correlated gain requires raw 1920 Hz Helper-off CPU IQ, a single-tap public model, zero clocks and the full +/-5 Hz band");
    const auto samples = slot.stop_sample - slot.first_sample;
    const auto timings = hf_public_timing_count(config, slot, Plan::maximum_timings);
    const std::uint64_t timing_bits = 2 * static_cast<std::uint64_t>(timings);
    require(samples <= Plan::maximum_basis_products / timing_bits && samples <= SIZE_MAX,
        "HF correlated gain public aperture exceeds its declared basis work cap");
    const auto groups = 1 + (samples - 1) / Plan::group_rows;
    // The basis cap also bounds timing_bits * groups because groups <= samples.
    const auto transforms = timing_bits * groups;
    constexpr std::uint64_t stages_per_transform = Plan::fft_rows * std::countr_zero(Plan::fft_rows);
    constexpr std::uint64_t updates_per_transform = Plan::frequency_count * Plan::laws;
    require(transforms <= Plan::maximum_fft_row_stages / stages_per_transform &&
        transforms <= Plan::maximum_innovations / updates_per_transform,
        "HF correlated gain public aperture exceeds its declared FFT or likelihood work caps");
    return {static_cast<std::size_t>(samples), static_cast<std::size_t>(groups), timings,
            timing_bits * samples, transforms * stages_per_transform, transforms * updates_per_transform};
}

HfGainInnovation hf_gain_innovation(double& predicted_covariance, double gram, double power, double rho) {
    const auto covariance = predicted_covariance;
    const auto scale = 1 / (1 + covariance * gram);
    const auto penalty = std::log1p(covariance * gram);
    const auto rho2 = rho * rho;
    predicted_covariance = rho2 * covariance * scale + (1 - rho2) * power;
    return {covariance, scale, penalty, rho * scale, gram};
}

double HfGainInnovation::accumulate(Complex& mean, Complex q) const {
    const double projection = mean.real() * q.real() + mean.imag() * q.imag();
    const double exponent = covariance * std::norm(q) + 2 * projection - gram * std::norm(mean);
    mean = mean_transport * (mean + covariance * q);
    return inverse_precision * exponent - penalty;
}

struct HfGainFrequencyProjection::Impl {
    fft::DoublePlan transform;
    std::array<Complex, Plan::atoms_per_group> translate;
    std::vector<Complex> global;

    Impl(std::size_t groups, std::stop_token stop)
        : transform(Plan::fft_rows, stop), global(checked_multiply(groups, Plan::frequency_count)) {
        for (std::size_t i = 0; i < translate.size(); ++i)
            translate[i] = std::polar(1., 2 * pi * Plan::frequency_bound_hz * i * Plan::atom_rows / Plan::sample_rate);
        for (std::size_t group = 0; group < groups; ++group) {
            check_cancelled(stop);
            const double seconds = (group * Plan::group_rows + (Plan::atom_rows - 1) * .5) / Plan::sample_rate;
            for (std::size_t f = 0; f < Plan::frequency_count; ++f)
                global[group * Plan::frequency_count + f] = std::polar(1., -2 * pi * frequency(f) * seconds);
        }
    }
};

HfGainFrequencyProjection::HfGainFrequencyProjection(std::size_t groups, std::stop_token stop)
    : impl_(std::make_unique<const Impl>(groups, stop)) {}
HfGainFrequencyProjection::~HfGainFrequencyProjection() = default;

std::size_t HfGainFrequencyProjection::workspace_bytes(std::size_t groups) {
    const auto count = checked_add(Plan::fft_rows, checked_multiply(groups, Plan::frequency_count));
    return checked_add(sizeof(Impl), checked_multiply(count, sizeof(Complex)));
}

std::span<const Complex> HfGainFrequencyProjection::project(std::size_t group, std::span<const Complex> atoms,
        std::span<Complex> scratch, std::stop_token stop) const {
    require(group < impl_->global.size() / Plan::frequency_count && !atoms.empty() &&
        atoms.size() <= Plan::atoms_per_group && scratch.size() == Plan::fft_rows,
        "HF correlated gain frequency projection received invalid public geometry");
    for (std::size_t i = 0; i < atoms.size(); ++i) scratch[i] = atoms[i] * impl_->translate[i];
    std::ranges::fill(scratch.subspan(atoms.size()), Complex{});
    fft::fft_prefix(scratch, false, impl_->transform, Plan::frequency_count, stop);
    for (std::size_t f = 0; f < Plan::frequency_count; ++f)
        scratch[f] *= impl_->global[group * Plan::frequency_count + f];
    return scratch.first(Plan::frequency_count);
}

std::size_t hf_correlated_gain_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    const auto plan = hf_correlated_gain_plan(config, slot);
    require(workers > 0, "HF correlated gain requires a positive admitted lane count");
    const auto active = static_cast<unsigned>(std::min<std::size_t>(workers, plan.timings));
    const auto lane_bytes = checked_multiply(active, checked_add(sizeof(Lane), HfPublicBasisPair::workspace_bytes(config, slot)));
    const auto basis_bytes = MappedBasis::workspace_bytes(config, slot, Plan::group_rows, active);
    const auto shared_bytes = HfGainFrequencyProjection::workspace_bytes(plan.groups);
    // Include bounded per-coordinate fallback reason storage and vector headers.
    const auto timings_bytes = checked_multiply(plan.timings, sizeof(TimingEvidence) + 2 * sizeof(FineCoordinate) + 128);
    return checked_add(4096, checked_add(checked_add(lane_bytes, basis_bytes), checked_add(shared_bytes, timings_bytes)));
}

HfCorrelatedGainEvidence score_hf_correlated_gain(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const datapump::Crypto& key, std::span<const Complex> iq, const FinePreparation& prepared,
        unsigned workers, std::size_t admitted_bytes, std::stop_token stop, ReceiverExecutor& executor) {
    const auto start = Clock::now();
    const auto plan = hf_correlated_gain_plan(config, slot);
    const auto scratch = hf_correlated_gain_workspace(config, slot, workers);
    require(iq.size() == plan.samples, "HF correlated gain needs a complete public observation");
    if (scratch > admitted_bytes) throw ReceiverWorkspaceError("HF correlated gain exceeds the admitted stage workspace");
    const auto active = static_cast<unsigned>(std::min<std::size_t>(workers, plan.timings));
    for (std::size_t i = 0; i < iq.size(); ++i) {
        if (i % 4096 == 0) check_cancelled(stop);
        require(std::isfinite(iq[i].real()) && std::isfinite(iq[i].imag()), "HF correlated gain requires finite IQ");
    }
    const auto selected = select_hf_public_timing(config, slot, prepared, false, false, Plan::maximum_timings);
    const auto full = select_hf_public_timing(config, slot, prepared, true, false, Plan::maximum_timings);
    require(full.coordinates.size() == plan.timings, "HF correlated gain timing grid disagrees with its quote");
    const HfGainFrequencyProjection projection(plan.groups, stop);
    const auto laws = gain_laws();
    MappedBasis basis(config, slot, key, Plan::group_rows, active);
    std::vector<Lane> lanes(active);
    std::vector<TimingEvidence> scores(plan.timings);
    executor.parallel(slot.index, plan.timings, active, stop, [&](unsigned lane, std::size_t i, std::stop_token leaf) {
        scores[i] = score_timing(config, slot, key, iq, full.coordinates[i], basis, projection, laws, lanes[lane], leaf);
    });

    std::array<double, 2> inside{absent, absent}, outside = inside, maximum = inside, totals;
    std::array<std::array<double, 2>, Plan::laws> by_law;
    by_law.fill(inside);
    TimingEvidence actual;
    const auto log_timing_weight = -std::log(static_cast<double>(plan.timings));
    for (std::size_t i = 0; i < scores.size(); ++i) {
        const auto& timing = scores[i];
        actual.basis_products += timing.basis_products;
        actual.fft_transforms += timing.fft_transforms;
        actual.innovations += timing.innovations;
        actual.basis_seconds += timing.basis_seconds;
        actual.projection_seconds += timing.projection_seconds;
        actual.innovation_seconds += timing.innovation_seconds;
        actual.rendering.bank_calls += timing.rendering.bank_calls;
        actual.rendering.bank_column_samples += timing.rendering.bank_column_samples;
        actual.rendering.fallback_calls += timing.rendering.fallback_calls;
        actual.rendering.coordinate_guard_fallbacks += timing.rendering.coordinate_guard_fallbacks;
        auto& group = std::ranges::find(selected.coordinates, full.coordinates[i]) != selected.coordinates.end() ? inside : outside;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            group[bit] = log_add(group[bit], scores[i].mixture[bit] + log_timing_weight);
            maximum[bit] = std::max(maximum[bit], scores[i].maximum[bit]);
            for (std::size_t law = 0; law < Plan::laws; ++law)
                by_law[law][bit] = log_add(by_law[law][bit], scores[i].per_law[law][bit] + log_timing_weight);
        }
    }
    const auto cache = basis.report();
    require(actual.basis_products == plan.basis_products && actual.innovations == plan.innovations &&
        actual.fft_transforms * Plan::fft_rows * std::countr_zero(Plan::fft_rows) == plan.fft_row_stages,
        "HF correlated gain actual work did not cover its full public quote");
    for (std::size_t bit = 0; bit < 2; ++bit) totals[bit] = log_add(inside[bit], outside[bit]);
    HfCorrelatedGainEvidence result{plan, slot.index, selected.coordinates.size(), scratch, workers, active,
        config.noise_variance, config.time_varying_minimum_bit_log_separation,
        std::chrono::duration<double>(Clock::now() - start).count(), totals, inside, outside, maximum, by_law, {}};
    result.work = {actual.basis_products, actual.fft_transforms, actual.innovations, actual.rendering.bank_calls,
        actual.rendering.bank_column_samples, actual.rendering.fallback_calls, actual.rendering.coordinate_guard_fallbacks,
        cache.calls, cache.direct_rows, cache.interpolated_rows, cache.peak_cache_bytes,
        actual.basis_seconds, actual.projection_seconds, actual.innovation_seconds};
    return result;
}

namespace {
Json evidence_json(const HfCorrelatedGainEvidence& score) {
    const auto& plan = score.plan;
    const auto& totals = score.bit_log_e;
    const auto& by_law = score.per_law;
    const auto& actual = score.work;
    const auto presence = log_add(totals[0], totals[1]) - std::numbers::ln2;
    const auto charged = presence + std::log(charged_supplement_weight);
    const auto gap = std::abs(totals[0] - totals[1]);
    const auto threshold = receiver_operational_threshold();
    Json::Array coherence, powers, law_evidence;
    for (auto seconds : Plan::coherence_seconds) coherence.push_back(seconds);
    for (auto power : Plan::gain_powers) powers.push_back(power);
    for (std::size_t law = 0; law < by_law.size(); ++law) {
        law_evidence.push_back(object({{"coherence_seconds", Plan::coherence_seconds[law / Plan::gain_powers.size()]},
            {"gain_power", Plan::gain_powers[law % Plan::gain_powers.size()]},
            {"weight", 1. / Plan::laws}, {"bit_log_e_before_law_weight", pair_json(by_law[law])}}));
    }
    return object({{"component_complete", true}, {"slot_index", score.slot_index}, {"receiver_executed", false},
        {"model", "stationary_single_tap_complex_ar1"},
        {"completion_evaluated", false}, {"capacity_qualified", false}, {"truth_information_passed_to_search", false},
        {"all_public_timing_atoms_evaluated", true}, {"public_timing_atoms", plan.timings},
        {"canonical_selected_timing_atoms", score.selected_timings}, {"observation_samples", plan.samples},
        {"sample_rate_hz", Plan::sample_rate}, {"phase_atom_rows", Plan::atom_rows}, {"gain_block_rows", Plan::group_rows},
        {"gain_blocks", plan.groups}, {"frequency_count", Plan::frequency_count}, {"fft_rows", Plan::fft_rows},
        {"frequency_bound_hz", Plan::frequency_bound_hz}, {"frequency_step_hz", Plan::frequency_step_hz},
        {"maximum_nominal_phase_atom_error_radians", pi * Plan::frequency_bound_hz * Plan::atom_rows / Plan::sample_rate},
        {"closed_band_frequency_prior", "uniform cell widths with half mass at both endpoints"},
        {"coherence_seconds", std::move(coherence)}, {"gain_powers", std::move(powers)}, {"noise_variance", score.noise_variance},
        {"per_law_evidence", std::move(law_evidence)},
        {"bit_log_e", pair_json(totals)}, {"canonical_selected_bit_log_contribution", pair_json(score.selected_log_contribution)},
        {"complement_bit_log_contribution", pair_json(score.complement_log_contribution)},
        {"bitwise_maximum_unweighted_component_log_lr", pair_json(score.maximum_log_lr)},
        {"favored_bit", totals[1] > totals[0] ? 1 : 0}, {"bit_separation", gap},
        {"presence_log_e", presence}, {"standalone_native_weight", charged_supplement_weight},
        {"charged_presence_log_e", charged}, {"presence_reference_threshold", threshold},
        {"bit_separation_reference", score.bit_separation_reference},
        {"passes_standalone_candidate_screen", charged >= threshold && gap > 0 &&
            gap >= score.bit_separation_reference},
        {"basis_products", plan.basis_products}, {"full_fft_row_stage_charge", plan.fft_row_stages},
        {"scalar_innovations", plan.innovations},
        {"actual_work", object({{"basis_products", actual.basis_products}, {"fft_prefix_calls", actual.fft_prefix_calls},
            {"scalar_innovations", actual.innovations}, {"basis_bank_calls", actual.basis_bank_calls},
            {"basis_bank_column_samples", actual.basis_bank_column_samples},
            {"basis_fallback_calls", actual.basis_fallback_calls},
            {"coordinate_guard_fallbacks", actual.coordinate_guard_fallbacks},
            {"mapped_basis_calls", actual.mapped_basis_calls}, {"mapped_basis_direct_rows", actual.mapped_basis_direct_rows},
            {"mapped_basis_interpolated_rows", actual.mapped_basis_interpolated_rows},
            {"basis_cache_peak_bytes", actual.basis_cache_peak_bytes}})},
        {"summed_worker_wall_seconds", object({{"basis_rendering", actual.basis_seconds},
            {"correlation_and_projection", actual.projection_seconds}, {"gain_innovations", actual.innovation_seconds}})},
        {"diagnostic_workspace_bytes", score.workspace_bytes}, {"workers", score.workers}, {"active_timing_lanes", score.active_lanes},
        {"scoring_seconds", score.scoring_seconds},
        {"scope", "finite stationary one-tap complex Gaussian gain likelihood under known proper-complex IID white noise; full closed public timing and frequency prior; one eighth standalone native mass includes optional line comparison"}});
}
} // namespace

Json diagnose_hf_correlated_gain(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
        const datapump::Crypto& key, std::span<const Complex> iq, unsigned workers,
        std::size_t total_memory_limit_bytes, std::stop_token stop) {
    const auto start = Clock::now();
    const auto scratch = hf_correlated_gain_workspace(config, slot, workers);
    constexpr std::size_t metadata = 65536;
    const auto stacks = checked_multiply(workers - 1, datapump::execution::worker_stack_bytes());
    const auto footprint = std::max(datapump::execution::process_footprint_bytes(), iq.size_bytes());
    const auto base = checked_add(checked_add(footprint, stacks), metadata);
    if (base >= total_memory_limit_bytes || scratch > total_memory_limit_bytes - base)
        throw ReceiverWorkspaceError("HF correlated gain full search exceeds the admitted memory limit");
    const auto available = total_memory_limit_bytes - base;
    const FrequencyInterval prior{-config.frequency_bound_hz, config.frequency_bound_hz};
    if (coarse_work_plan(config, slot, {&prior, 1}, workers).workspace_reserved_bytes > available)
        throw ReceiverWorkspaceError("HF correlated gain canonical coarse plan exceeds the admitted memory limit");
    ReceiverExecutor executor(workers);
    const auto coarse = score_coarse(config, slot, key, iq, {&prior, 1}, workers, available, stop, &executor);
    const auto prepared = prepare_fine_coordinates(config, slot, coarse);
    const auto preparation_seconds = std::chrono::duration<double>(Clock::now() - start).count();
    auto report = evidence_json(score_hf_correlated_gain(config, slot, key, iq, prepared, workers, available, stop, executor));
    report["process_and_stacks_reserved_bytes"] = base;
    report["coarse_workspace_reserved_bytes"] = coarse.work.plan.workspace_reserved_bytes;
    report["memory_limit_bytes"] = total_memory_limit_bytes;
    report["canonical_preparation_seconds"] = preparation_seconds;
    return report;
}

struct HfCorrelatedGainReceiver::Impl {
    explicit Impl(std::size_t maximum) : slots(maximum) {}
    std::mutex mutex;
    std::vector<std::optional<HfCorrelatedGainEvidence>> slots;
    std::uint64_t overflow = 0, duplicate = 0;
};

HfCorrelatedGainReceiver::HfCorrelatedGainReceiver(std::size_t maximum_slot_reports) {
    require(maximum_slot_reports <= 65536, "HF correlated gain requires a bounded telemetry horizon");
    static_assert(sizeof(HfCorrelatedGainEvidence) < 1024);
    impl_ = std::make_shared<Impl>(maximum_slot_reports);
}

SlotEvidenceSupplement HfCorrelatedGainReceiver::supplement() const {
    SlotEvidenceSupplement result;
    result.fixed_weight = .25;
    result.supported = [](const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
        try { (void)hf_correlated_gain_plan(config, slot); }
        catch (const datapump::Error&) { return false; }
        return true;
    };
    result.workspace = hf_correlated_gain_workspace;
    result.score = [owner=impl_](const ReceiverConfig& config, const ReceiverSlotPlan& slot,
            const datapump::Crypto& key, std::span<const Complex> iq, const FinePreparation& prepared,
            unsigned workers, std::size_t admitted, std::stop_token stop, ReceiverExecutor& executor) {
        auto score = score_hf_correlated_gain(config, slot, key, iq, prepared, workers, admitted, stop, executor);
        const auto bits = score.bit_log_e;
        const std::lock_guard lock(owner->mutex);
        const auto saturate = [](std::uint64_t& count) { if (count < UINT64_MAX) ++count; };
        if (slot.index >= owner->slots.size()) saturate(owner->overflow);
        else if (owner->slots[slot.index]) saturate(owner->duplicate);
        else owner->slots[slot.index] = std::move(score);
        return bits;
    };
    return result;
}

bool HfCorrelatedGainReceiver::scored(std::uint64_t slot_index) const {
    const std::lock_guard lock(impl_->mutex);
    return slot_index < impl_->slots.size() && impl_->slots[slot_index].has_value();
}

Json HfCorrelatedGainReceiver::report() const {
    const std::lock_guard lock(impl_->mutex);
    Json::Array reports;
    for (const auto& slot : impl_->slots) if (slot) reports.push_back(evidence_json(*slot));
    return object({{"experimental", true}, {"fixed_weight", .25}, {"canonical_preparation_reused", true},
        {"telemetry_overflow", impl_->overflow},
        {"telemetry_duplicates", impl_->duplicate}, {"scored_components", std::move(reports)},
        {"scope", "component evidence returned to the canonical fallback; native slot receipts retain final admission, line disposal and quiet decisions"}});
}

} // namespace appraisal
