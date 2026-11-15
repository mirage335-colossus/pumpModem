#include "hf_normalized_null.hpp"

#include "datapump/execution_resources.hpp"
#include "triad_receiver_basis.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_driver.hpp"
#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>

extern "C" int triad_gaussian_update_v1(std::size_t taps, std::size_t candidates, std::size_t models,
    std::size_t blocks, std::size_t first, std::size_t stop, const double* correlations, const double* grams,
    const double* power, const double* transitions, double* means, double* covariance, double* scores,
    double* diagnostics) noexcept;

namespace appraisal {
namespace {
using namespace datapump::triad::v3;
using namespace receiver_detail;
using datapump::triad::probe::Json;
using datapump::triad::probe::object;
using normalized_null_detail::BitStatistics;
using normalized_null_detail::GainShape;
using normalized_null_detail::SpanEnergy;
using normalized_null_detail::Statistics;
using normalized_null_detail::Terms;
using normalized_null_detail::contrast_bank;
using normalized_null_detail::physical_power;
using Clock = std::chrono::steady_clock;

constexpr auto covariance_gain_scope = "independent tap gain vectors across disjoint DFT blocks";
constexpr std::size_t maximum_proposals = 12;
constexpr std::size_t receipt_bytes = 512 * 1024;
constexpr double absent = -std::numeric_limits<double>::infinity();
constexpr double relative_rank_floor = 64 * std::numeric_limits<double>::epsilon();

void require(bool valid, const char* reason) {
    if (!valid) throw datapump::Error(reason);
}

double log_add(double a, double b) {
    if (a == absent || b == absent) return std::max(a, b);
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}

struct DftBlock {
    DftPlan plan;
    std::size_t rows;
    double factor;
    std::vector<std::size_t> bins;

    DftBlock(std::size_t count, double fs, double half_band, std::stop_token stop)
        : plan(count, stop), rows(count), factor(1 / std::sqrt(count)) {
        bins.reserve(count);
        const auto spacing = fs / count;
        for (std::size_t i = 0; i < count; ++i) {
            const auto bin = i < (count + 1) / 2 ? static_cast<std::int64_t>(i) : -static_cast<std::int64_t>(count - i);
            if (std::abs(bin * spacing) <= half_band + spacing / 2) bins.push_back(i);
        }
    }

    void apply(std::span<Complex> data, std::span<Complex> scratch, std::stop_token stop) const {
        plan.transform(data, false, scratch, stop);
        for (const auto bin : bins) data[bin] *= factor;
    }
};

double public_half_band(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    return .5 * geometry(slot.format).occupied_bandwidth_hz * (1 + config.clock_bound_ppm * 1e-6) +
        std::abs(config.spread_doppler_hz) + 3 * config.random_doppler_width_hz +
        config.delay_motion_doppler_bound_hz + config.frequency_bound_hz + config.selective_modulation_rate_hz +
        config.time_varying_atmospheric_hz + config.source_spectral_skirt_allowance_hz;
}

struct Projection {
    DftBlock block;
    std::optional<DftBlock> tail;
    std::vector<Complex> modes;
    double energy = 0;

    Projection(std::span<const Complex> input, std::size_t block_rows, std::size_t scratch_rows,
               double fs, double half_band, std::stop_token stop)
        : block(block_rows, fs, half_band, stop) {
        if (const auto rest = input.size() % block_rows) tail.emplace(rest, fs, half_band, stop);
        modes.reserve(input.size() / block.rows * block.bins.size() + (tail ? tail->bins.size() : 0));
        std::vector<Complex> data(block_rows), scratch(scratch_rows);
        for (std::size_t first = 0; first < input.size(); first += block.rows) {
            const auto& shape = get(input.size() - first);
            auto data_block = std::span(data).first(shape.rows);
            std::ranges::copy(input.subspan(first, shape.rows), data_block.begin());
            shape.apply(data_block, scratch, stop);
            for (const auto bin : shape.bins) {
                energy += std::norm(data_block[bin]);
                modes.push_back(data_block[bin]);
            }
        }
    }

    const DftBlock& get(std::size_t remaining) const { return remaining >= block.rows ? block : *tail; }
};

struct Scratch {
    std::vector<Complex> templates, dft;
    std::vector<double> offsets;
    std::vector<BitStatistics> blocks;

    Scratch(std::size_t rows, std::size_t dft_rows, std::size_t taps, std::size_t blocks_count)
        : templates(2 * taps * rows), dft(dft_rows), offsets(rows), blocks(blocks_count) {
        std::iota(offsets.begin(), offsets.end(), 0.);
    }
};

void project_templates(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                       const FineCoordinate& coordinate, const Projection& y, MappedBasis& source,
                       std::span<const Tap> paths, Scratch& scratch, std::stop_token stop) {
    const auto scale = 1 + coordinate.clock_ppm * 1e-6;
    const auto arrival = coordinate.arrival_seconds + slot.window_capture_lead_seconds + slot.source_start_seconds / scale;
    const MappedBasisRow row{coordinate.epoch_offset_seconds, static_cast<double>(arrival),
        coordinate.frequency_hz - config.data_if_hz * (scale - 1), (scale - 1) * 1e6};
    const auto first_iq = slot.quiet_first_sample - slot.first_sample;
    const auto rows = slot.quiet_stop_sample - slot.quiet_first_sample;
    const auto taps = paths.size();
    std::size_t first_mode = 0;
    for (std::size_t first = 0, i = 0; first < rows; first += y.block.rows, ++i) {
        check_cancelled(stop);
        const auto& b = y.get(rows - first);
        const auto observed = std::span(y.modes).subspan(first_mode, b.bins.size());
        const auto pair = std::span(scratch.templates).first(2 * taps * b.rows);
        source.render_pair(row, paths, first_iq + first, std::span(scratch.offsets).first(b.rows), pair, stop);
        for (std::size_t col = 0; col < 2 * taps; ++col) b.apply(pair.subspan(col * b.rows, b.rows), scratch.dft, stop);
        scratch.blocks[i] = {};
        for (std::size_t bit = 0; bit < 2; ++bit) {
            auto& stats = scratch.blocks[i][bit];
            for (std::size_t a = 0; a < taps; ++a) {
                const auto lhs = pair.subspan((bit * taps + a) * b.rows, b.rows);
                for (std::size_t j = 0; j < b.bins.size(); ++j) stats.q[a] += std::conj(lhs[b.bins[j]]) * observed[j];
                for (std::size_t c = a; c < taps; ++c) {
                    const auto rhs = pair.subspan((bit * taps + c) * b.rows, b.rows);
                    for (const auto bin : b.bins) stats.gram[2 * a + c] += std::conj(lhs[bin]) * rhs[bin];
                    stats.gram[2 * c + a] = std::conj(stats.gram[2 * a + c]);
                }
            }
        }
        first_mode += b.bins.size();
    }
    require(first_mode == y.modes.size(), "normalized null template and observation DFT projections differ");
}

void add_span(const Statistics& s, std::size_t taps, SpanEnergy& out) {
    const auto e0 = s.gram[0].real(), e1 = taps == 2 ? s.gram[3].real() : 0;
    out.trace += e0 + e1;
    const std::size_t a = e0 < e1 ? 1 : 0, b = 1 - a;
    const auto pivot = s.gram[3 * a].real();
    if (pivot <= 0) return;
    ++out.rank;
    out.captured += std::norm(s.q[a]) / pivot;
    if (taps == 1 || s.gram[3 * b].real() == 0) return;
    const auto scale = s.gram[2 * b + a] / pivot;
    const auto remaining = s.gram[3 * b].real() - std::norm(s.gram[2 * b + a]) / pivot;
    if (remaining > relative_rank_floor * pivot) {
        ++out.rank;
        out.captured += std::norm(s.q[b] - scale * s.q[a]) / remaining;
    } else ++out.near_singular_blocks;
}

using Models = std::array<std::array<GainShape, 2>, 2>; // [tap count minus one][bit]

void score_models(std::span<const BitStatistics> stats, std::size_t taps, double variance,
                  Models& models, std::stop_token stop) {
    for (std::size_t tap_count = 1; tap_count <= taps; ++tap_count)
        for (std::size_t bit = 0; bit < 2; ++bit)
            models[tap_count - 1][bit] = normalized_null_detail::score_gain_shape(stats, bit, tap_count, variance, stop);
}

std::vector<FineCoordinate> coordinates(const FinePreparation& prepared) {
    std::vector<FineCoordinate> result;
    result.reserve(maximum_proposals);
    const auto add = [&](const FineCoordinate& one) {
        if (result.size() < maximum_proposals && std::ranges::find(result, one) == result.end()) result.push_back(one);
    };
    for (const auto& one : std::span(prepared.coordinates).first(std::min(maximum_proposals / 2, prepared.coordinates.size()))) add(one);
    for (const auto& one : prepared.varying_coordinates)
        if (std::ranges::find(prepared.coordinates, one) == prepared.coordinates.end()) add(one);
    for (const auto& one : prepared.coordinates) add(one);
    return result;
}

Json models_json(std::span<const FineCoordinate> atoms, std::span<const Models> bank, const Projection& y,
                 std::size_t taps, double grid_log_count) {
    std::array<double, 2> angular{absent, absent}, gaussian{absent, absent};
    const auto contrast_weight = -grid_log_count - std::log(double(taps * contrast_bank.size()));
    const auto physical_weight = -grid_log_count - std::log(double(taps * physical_power.size()));
    const auto d = y.modes.size();
    Json::Array rows;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        Json::Array models;
        for (std::size_t tap = 0; tap < taps; ++tap) {
            for (std::size_t bit = 0; bit < 2; ++bit) {
                const auto& m = bank[i][tap][bit];
                Json::Array contrast, physical;
                for (std::size_t k = 0; k < contrast_bank.size(); ++k) {
                    const auto value = m.angular[k];
                    const auto log_e = normalized_null_detail::direction_score(d, y.energy, value);
                    angular[bit] = log_add(angular[bit], contrast_weight + log_e);
                    contrast.push_back(object({{"contrast", contrast_bank[k]}, {"log_e", log_e},
                        {"explained_fraction", value.explained / y.energy}, {"log_determinant", value.determinant}}));
                }
                for (std::size_t k = 0; k < physical_power.size(); ++k) {
                    const auto value = m.gaussian[k];
                    gaussian[bit] = log_add(gaussian[bit], physical_weight + value.explained - value.determinant);
                    physical.push_back(object({{"total_gain_power_per_block", physical_power[k]},
                        {"log_e", value.explained - value.determinant}, {"explained_quadratic", value.explained},
                        {"log_determinant", value.determinant}}));
                }
                const auto comparison_fraction = m.span.captured / y.energy;
                require(comparison_fraction <= 1 + relative_rank_floor, "normalized null projector energy exceeds observation norm");
                const auto fraction = std::min(1., comparison_fraction);
                const auto bound = m.span.near_singular_blocks ? 0. :
                    normalized_null_detail::projector_bound(d, m.span.rank, fraction);
                models.push_back(object({{"bit", bit}, {"tap_count_per_block", tap + 1},
                    {"independent_gain_blocks", m.independent_gain_blocks}, {"total_template_rank", m.span.rank},
                    {"covariance_gain_scope", covariance_gain_scope},
                    {"template_gram_trace", m.span.trace}, {"span_energy_fraction", fraction},
                    {"rank_conditioning_ambiguous_blocks", m.span.near_singular_blocks},
                    {"optimized_isotropic_projector_log_ratio", std::isfinite(bound) && !m.span.near_singular_blocks ? Json(bound) : Json{}},
                    {"isotropic_projector_bound_status", m.span.near_singular_blocks ? "rank_conditioning_ambiguous" :
                        std::isfinite(bound) ? "finite" : "unbounded_exact_containment"},
                    {"contrast_log_prior_before_bit", contrast_weight}, {"physical_log_prior_before_bit", physical_weight},
                    {"normalized_angular", std::move(contrast)}, {"known_white", std::move(physical)}}));
            }
        }
        const auto& one = atoms[i];
        rows.push_back(object({{"epoch_offset_seconds", one.epoch_offset_seconds}, {"arrival_seconds", one.arrival_seconds},
            {"frequency_hz", one.frequency_hz}, {"clock_ppm", one.clock_ppm}, {"models", std::move(models)}}));
    }
    return object({{"directional_bit_log_e", Json::Array{angular[0], angular[1]}},
        {"known_white_bit_log_e", Json::Array{gaussian[0], gaussian[1]}},
        {"directional_equal_bit_mixture_log_e", log_add(angular[0], angular[1]) - std::log(2.)},
        {"known_white_equal_bit_mixture_log_e", log_add(gaussian[0], gaussian[1]) - std::log(2.)},
        {"grid_log_count", grid_log_count}, {"selected_coordinate_prior_mass", atoms.size() * std::exp(-grid_log_count)},
        {"coordinates", std::move(rows)}});
}
} // namespace

namespace normalized_null_detail {
GainShape score_gain_shape(std::span<const BitStatistics> stats, std::size_t bit, std::size_t taps,
                           double variance, std::stop_token stop) {
    require(bit < 2 && (taps == 1 || taps == 2), "invalid normalized-null block gain geometry");
    GainShape model;
    model.independent_gain_blocks = stats.size();
    for (const auto& block : stats) add_span(block[bit], taps, model.span);
    if (!model.span.rank) return model; // Identity covariance for an empty template.
    const double normalization = model.span.rank / model.span.trace;
    const auto add = [](Terms& a, Terms b) { a.explained += b.explained; a.determinant += b.determinant; };
    for (const auto& block : stats) {
        check_cancelled(stop);
        for (std::size_t c = 0; c < contrast_bank.size(); ++c)
            add(model.angular[c], gaussian_terms(block[bit], taps, 1., contrast_bank[c] * normalization));
        for (std::size_t p = 0; p < physical_power.size(); ++p)
            add(model.gaussian[p], gaussian_terms(block[bit], taps, variance, physical_power[p] / taps));
    }
    return model;
}

Terms gaussian_terms(const Statistics& s, std::size_t taps, double variance, double power) {
    require((taps == 1 || taps == 2) && std::isfinite(variance) && variance > 0 && std::isfinite(power) && power > 0,
        "invalid normalized-null Gaussian model");
    std::array<Complex, 6> q{}, means{};
    std::array<Complex, 12> gram{}, covariance{};
    const auto rows = taps * taps;
    // Read both the actual quadratic and the zero-observation determinant
    // from the native three-hypothesis kernel, without subtractive cancellation.
    for (std::size_t i = 0; i < taps; ++i) {
        q[taps + i] = s.q[i] / variance;
        for (std::size_t j = 0; j < taps; ++j)
            gram[rows + taps * i + j] = gram[2 * rows + taps * i + j] = s.gram[2 * i + j] / variance;
    }
    const std::array powers{power, power};
    const std::array transition{0., 1., 0., 0.};
    std::array<double, 3> scores{};
    std::array<double, 2> diagnostics{};
    const auto status = triad_gaussian_update_v1(taps, 1, 1, 1, 0, 1, reinterpret_cast<const double*>(q.data()),
        reinterpret_cast<const double*>(gram.data()), powers.data(), transition.data(), reinterpret_cast<double*>(means.data()),
        reinterpret_cast<double*>(covariance.data()), scores.data(), diagnostics.data());
    require(!status && scores[0] == 0, "normalized null native Gaussian update failed");
    Terms out{0., -scores[2]};
    for (std::size_t i = 0; i < taps; ++i) out.explained += (std::conj(q[taps + i]) * means[taps + i]).real();
    return out;
}

double direction_score(std::size_t modes, double energy, Terms value) {
    require(modes > 0 && std::isfinite(energy) && energy > 0 && value.explained >= 0 && value.explained < energy &&
        std::isfinite(value.determinant) && value.determinant >= 0, "normalized null requires 0 <= J < R");
    return -value.determinant - modes * std::log1p(-value.explained / energy);
}

double projector_bound(std::size_t modes, std::size_t rank, double eta) {
    require(modes > 0 && rank <= modes && eta >= 0 && eta <= 1., "invalid normalized null projector geometry");
    const auto fraction = double(rank) / modes;
    if (!rank || eta <= fraction) return 0;
    if (eta == 1.) return std::numeric_limits<double>::infinity();
    return modes * (fraction * std::log(fraction / eta) +
        (1 - fraction) * (std::log1p(-fraction) - std::log1p(-eta)));
}
} // namespace normalized_null_detail

Json diagnose_hf_normalized_null(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const datapump::Crypto& key,
                                  std::span<const Complex> iq, unsigned workers, std::size_t memory, std::stop_token stop) {
    const auto began = Clock::now();
    require(!config.helper_enabled && !config.bulk_backend && !config.physical_rf_retuning &&
        config.sample_rate_hz == 1920 && workers > 0 && std::isfinite(config.noise_variance) && config.noise_variance > 0,
        "normalized null requires the 1920 Hz Helper-off native CPU HF aperture");
    require(config.time_varying_channel || (config.time_varying_atmospheric_hz == 0 &&
        config.time_varying_common_doppler_hz == 0 && config.selective_modulation_rate_hz == 0 &&
        config.source_spectral_skirt_allowance_hz == 0),
        "static normalized null requires the unchanged zero-extra protected-probe geometry");
    require(slot.index == 0 && slot.stop_sample > slot.first_sample && iq.size() == slot.stop_sample - slot.first_sample &&
        iq.size() <= hf_normalized_null_maximum_rows && slot.quiet_first_sample >= slot.first_sample &&
        slot.quiet_stop_sample > slot.quiet_first_sample && slot.quiet_stop_sample <= slot.stop_sample,
        "normalized null requires the complete bounded first public observation");
    const auto first = static_cast<std::size_t>(slot.quiet_first_sample - slot.first_sample);
    const auto rows = static_cast<std::size_t>(slot.quiet_stop_sample - slot.quiet_first_sample);
    const auto block_rows = std::min(rows, fine_block_geometry(config.sample_rate_hz).block_rows);
    const auto tail = rows % block_rows;
    const auto blocks = rows / block_rows + (tail != 0);
    const auto dft_rows = std::max(DftPlan::scratch_samples(block_rows), tail ? DftPlan::scratch_samples(tail) : 0);
    const auto half_band = public_half_band(config, slot);
    require(std::isfinite(half_band) && half_band > 0 && half_band < config.sample_rate_hz / 2,
        "normalized null DATA projection lies outside receiver Nyquist");
    const std::array<Tap, 2> paths{{{0, 0}, {config.spread_delay_seconds, config.spread_doppler_hz}}};
    const std::size_t taps = paths[1].delay_seconds || paths[1].doppler_hz ? 2 : 1;
    const auto max_lanes = static_cast<unsigned>(std::min<std::size_t>(workers, maximum_proposals));
    const auto resident = std::max(iq.size_bytes(), datapump::execution::process_footprint_bytes());
    const auto reserved = checked_add(checked_add(resident, receipt_bytes),
        checked_multiply(workers - 1, datapump::execution::worker_stack_bytes()));
    const auto projection_bytes = rows * sizeof(Complex) + (block_rows + tail) * sizeof(std::size_t) +
        DftPlan::workspace_bytes(block_rows) + (tail ? DftPlan::workspace_bytes(tail) : 0);
    const auto lane_bytes = (2 * taps * block_rows + dft_rows) * sizeof(Complex) + block_rows * sizeof(double) +
        blocks * sizeof(BitStatistics);
    const auto fine_bytes = checked_add(projection_bytes, checked_add(checked_multiply(max_lanes, lane_bytes),
        MappedBasis::workspace_bytes(config, slot, block_rows, max_lanes)));
    const FrequencyInterval full_prior{-config.frequency_bound_hz, config.frequency_bound_hz};
    const auto coarse_bytes = coarse_work_plan(config, slot, {&full_prior, 1}, workers).workspace_reserved_bytes;
    if (reserved >= memory || std::max(coarse_bytes, fine_bytes) > memory - reserved)
        throw ReceiverWorkspaceError("normalized null stages exceed their resident, stacks and workspace budget");

    ReceiverExecutor executor(workers);
    double log_grid_count = 0;
    std::size_t completed_tiles = 0;
    std::vector<FineCoordinate> atoms;
    {
        const auto coarse = score_coarse(config, slot, key, iq, {&full_prior, 1}, workers, memory - reserved, stop, &executor);
        completed_tiles = coarse.work.completed_tiles;
        const auto prepared = prepare_fine_coordinates(config, slot, coarse);
        atoms = coordinates(prepared);
        log_grid_count = AdmissionGrid(prepared.grid.lower, prepared.grid.upper,
            config.frequency_bound_hz + config.time_varying_atmospheric_hz,
            config.clock_bound_ppm, prepared.grid.frequency_step).log_count();
    }
    const auto coarse_done = Clock::now();
    require(!atoms.empty() && std::log(double(atoms.size())) <= log_grid_count, "normalized null proposal mass exceeds public grid");
    const Projection y(iq.subspan(first, rows), block_rows, dft_rows, config.sample_rate_hz, half_band, stop);
    require(y.modes.size() > 1 && y.energy > 0 && std::isfinite(y.energy), "normalized null requires nonzero finite projected IQ");
    const auto lanes = static_cast<unsigned>(std::min<std::size_t>(max_lanes, atoms.size()));
    MappedBasis source(config, slot, key, block_rows, lanes);
    std::vector<Scratch> scratch;
    scratch.reserve(lanes);
    for (unsigned i = 0; i < lanes; ++i) scratch.emplace_back(block_rows, dft_rows, taps, blocks);
    std::vector<Models> models(atoms.size());
    executor.parallel(slot.index, atoms.size(), lanes, stop, [&](unsigned lane, std::size_t i, std::stop_token token) {
        project_templates(config, slot, atoms[i], y, source, std::span(paths).first(taps), scratch[lane], token);
        score_models(scratch[lane].blocks, taps, config.noise_variance, models[i], token);
    });
    const auto peak = executor.peak_workers();
    executor.close();
    auto json = models_json(atoms, models, y, taps, log_grid_count);
    json["aperture"] = object({{"first_sample", slot.quiet_first_sample}, {"stop_sample", slot.quiet_stop_sample},
        {"data_half_band_hz", half_band}, {"block_rows", block_rows}, {"tail_rows", tail}, {"blocks", blocks},
        {"complex_modes", y.modes.size()}, {"raw_projected_iq_energy", y.energy}, {"noise_variance", config.noise_variance}});
    json["work"] = object({{"coarse_calls", 1}, {"completed_coarse_tiles", completed_tiles}, {"configured_workers", workers},
        {"peak_workers", peak}, {"angular_lanes", lanes}, {"basis_calls", source.report().calls},
        {"coarse_bytes", coarse_bytes}, {"shadow_bytes", fine_bytes}, {"reserved_bytes", reserved}, {"memory_limit_bytes", memory},
        {"coarse_seconds", std::chrono::duration<double>(coarse_done - began).count()},
        {"shadow_seconds", std::chrono::duration<double>(Clock::now() - coarse_done).count()}});
    json["changes_receiver_decisions"] = false;
    json["covariance_gain_scope"] = covariance_gain_scope;
    json["public_prior_selection"] = "fixed capped same-input native proposals retain complete public lattice mass per coordinate";
    json["scope"] = "one fixed common orthonormal public DATA-band projection; normalized covariance V=I+(contrast*rank/trace) AA^H; scalar projector optimum uses observed direction and is a diagnostic only; spherical null is unqualified for wrong-key HF";
    return json;
}
} // namespace appraisal
