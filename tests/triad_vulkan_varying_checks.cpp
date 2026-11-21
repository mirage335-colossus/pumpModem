// Vulkan varying statistics checks (test_triad_vulkan --varying): the
// selection policy, receipts and leases of the FP64 device session, and its
// statistics against the native FP64 projections (DftPlan with the native
// gathers and sums) on the 200/239-row HF and 1200/1505-row 60 Hz shapes,
// power-of-two lengths with and beside a longer final block, central and
// tracked atoms and a slot without a remainder. Predeclared bound: every
// statistic group within 1e-12 of its native norm; a zero group stays
// exactly zero. Then the receiver's device route in score_varying_fine on
// tracked and central independent atoms: per-batch native reruns are
// bit-identical to the native route, and device batches keep every counter
// and decision with every evidence term within 1e-8 nats.

#include "triad_vulkan_stage_checks.hpp"

#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_detail.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_fine.hpp"
#include "triad_vulkan_stages.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace datapump::triad::v3::test {
namespace {
namespace r = receiver_detail;
namespace vk = receiver_detail::vulkan;
using r::StageFamily;
using r::StageNative;
using r::StageReason;

constexpr double statistic_bound = 1e-12;
constexpr std::size_t workspace = 512 * 1024 * 1024;

void check(bool condition, const std::string& message) {
    if (!condition) throw Error(message);
}

std::string text(StageReason reason) { return std::string(vk::stage_reason_text(StageFamily::varying, reason)); }

r::StageDeviceReceipt stage(const r::ReceiverBulkBackend& backend) {
    return r::vulkan_stage_receipt(backend.receipt(), StageFamily::varying);
}

// Deterministic values: a 64-bit LCG to [-1, 1).
struct Values {
    std::uint64_t state;
    double next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state >> 11) * 0x1p-52 - 1;
    }
    Complex complex() { const auto re = next(); return {re, next()}; }
};

// One public length: its DFT plan, retained modes and the half-bins of the
// tracked states, as the receiver's projection and tracker form them. The
// lattice is the projected tracker's: a step of whole half-bins of the
// block length about the center spanning 60% of the band, which rounds to
// either parity on a longer final block.
struct Length {
    std::size_t rows = 0;
    r::DftPlan plan;
    std::vector<std::size_t> indices;
    std::vector<std::int64_t> bins;
    double scale = 0;
    Length(std::size_t n, std::size_t modes, std::size_t states, std::size_t block_rows)
        : rows(n), plan(n), scale(1 / std::sqrt(double(n))) {
        // DATA bins about DC with an excluded Helper gap, in DFT order.
        const auto positive = (modes + 1) / 2;
        for (std::size_t i = 0; i < positive + 1 && indices.size() < modes; ++i)
            if (i != 2) indices.push_back(i);
        for (std::size_t i = 0; indices.size() < modes; ++i) indices.push_back(n - modes + positive + i);
        std::ranges::sort(indices);
        check(std::ranges::adjacent_find(indices) == indices.end() && indices.back() < n,
              "varying fixture indices are not distinct DFT bins");
        constexpr double fs = 6000;
        const auto radius = static_cast<std::int64_t>(states / 2);
        const auto step_bins = std::max<std::size_t>(1, 6 * block_rows / (10 * std::max<std::size_t>(1, states / 2)));
        const auto step_hz = static_cast<double>(step_bins) * fs / (2. * static_cast<double>(block_rows));
        for (std::size_t state = 0; state < states; ++state)
            bins.push_back(r::projected_half_bin(n, fs, step_hz, radius, state));
    }
    r::VaryingProjectionTables tables() const { return {plan.chirp(), plan.kernel_spectrum(), indices, scale}; }
    // DftPlan's forward transform of one column.
    std::vector<Complex> transform(std::span<const Complex> column) const {
        std::vector<Complex> values(column.begin(), column.end()), scratch(r::DftPlan::scratch_samples(rows));
        plan.transform(values, false, scratch);
        return values;
    }
};

struct Fixture {
    std::string name;
    std::size_t block_rows, final_rows;
    std::array<std::size_t, 2> modes;
    std::vector<std::uint32_t> taps;   // affine items: states, then center atoms
    std::size_t center_atoms = 0, atoms = 0, states = 0, batch = 0, blocks = 0;
};

r::VaryingBulkShape shape_of(const Fixture& fixture, const std::array<Length, 2>& lengths) {
    r::VaryingBulkShape shape;
    shape.block_rows = fixture.block_rows; shape.final_rows = fixture.final_rows;
    shape.bluestein_rows = {lengths[0].plan.transform_rows(), lengths[1].plan.transform_rows()};
    shape.modes = fixture.modes;
    shape.independent_states = fixture.states; shape.affine_states = fixture.taps.size();
    shape.independent_atoms = fixture.atoms; shape.center_atoms = fixture.center_atoms;
    shape.taps = fixture.atoms || fixture.center_atoms ? 2 : 1;
    for (const auto taps : fixture.taps) {
        shape.taps = std::max<std::size_t>(shape.taps, taps);
        shape.affine_columns += 2 * taps;
    }
    shape.batch_blocks = fixture.batch;
    return shape;
}

// Native FP64 statistics of one block: Projection::apply and
// projected_statistics for the affine items, the projected tracker's full
// transforms and shifted sums for the tracked atoms.
struct BlockReference {
    std::vector<r::FineBulkStatistics> affine;
    std::vector<r::VaryingIndependentRow> cells;   // [atom][cell]
};
BlockReference reference(const Fixture& fixture, const Length& length, std::span<const Complex> templates,
                         std::span<const Complex> y, double variance) {
    BlockReference result;
    const auto rows = length.rows, modes = length.indices.size();
    std::size_t column = 0;
    for (const auto taps : fixture.taps) {
        std::array<std::vector<Complex>, 4> projected;
        for (std::size_t c = 0; c < 2 * taps; ++c) {
            const auto spectrum = length.transform(templates.subspan((column + c) * rows, rows));
            for (std::size_t i = 0; i < modes; ++i) projected[c].push_back(spectrum[length.indices[i]] * length.scale);
        }
        column += 2 * taps;
        auto& statistics = result.affine.emplace_back();
        for (std::size_t hypothesis = 1; hypothesis <= 2; ++hypothesis) {
            const auto offset = (hypothesis - 1) * taps;
            for (std::size_t tap = 0; tap < taps; ++tap) {
                const auto& a = projected[offset + tap];
                Complex correlation{};
                for (std::size_t i = 0; i < modes; ++i) correlation += std::conj(a[i]) * y[i];
                statistics.correlations[hypothesis * taps + tap] = correlation / variance;
                const auto start = hypothesis * taps * taps;
                for (std::size_t other = tap; other < taps; ++other) {
                    const auto& b = projected[offset + other];
                    Complex product{};
                    for (std::size_t i = 0; i < modes; ++i) product += std::conj(a[i]) * b[i];
                    product /= variance;
                    statistics.gram[start + tap * taps + other] = product;
                    statistics.gram[start + other * taps + tap] = std::conj(product);
                }
            }
        }
    }
    for (std::size_t atom = 0; atom < fixture.atoms; ++atom) {
        const auto first = column + 4 * atom;
        for (std::int64_t half = 0; half < 2; ++half) {
            std::array<std::vector<Complex>, 4> spectrum;
            const auto increment = std::polar(1., half * std::numbers::pi / rows);
            for (std::size_t c = 0; c < 4; ++c) {
                std::vector<Complex> values(rows);
                auto phase = Complex{1};
                for (std::size_t i = 0; i < rows; ++i) {
                    values[i] = templates[(first + c) * rows + i] * phase;
                    phase *= increment;
                }
                spectrum[c] = length.transform(values);
                for (auto& value : spectrum[c]) value *= length.scale;
            }
            for (std::size_t state = 0; state < fixture.states; ++state) {
                const auto bin = length.bins[state];
                if ((bin % 2 + 2) % 2 != half) continue;
                const auto shift = (bin - half) / 2;
                for (std::uint32_t bit = 0; bit < 2; ++bit) {
                    r::VaryingIndependentRow cell;
                    for (std::size_t i = 0; i < modes; ++i) {
                        auto at = static_cast<std::int64_t>(length.indices[i]) - shift;
                        if (at < 0) at += static_cast<std::int64_t>(rows);
                        if (at >= static_cast<std::int64_t>(rows)) at -= static_cast<std::int64_t>(rows);
                        const auto direct = spectrum[2 * bit][static_cast<std::size_t>(at)];
                        const auto echo = spectrum[2 * bit + 1][static_cast<std::size_t>(at)];
                        cell.correlations[0] += std::conj(direct) * y[i];
                        cell.diagonal[0] += std::norm(direct);
                        cell.correlations[1] += std::conj(echo) * y[i];
                        cell.diagonal[1] += std::norm(echo);
                        cell.cross += std::conj(direct) * echo;
                    }
                    cell.half_bin = bin; cell.bit = bit; cell.state = static_cast<std::uint32_t>(state);
                    result.cells.push_back(cell);
                }
            }
        }
    }
    return result;
}

// One group's norm-relative error; a zero reference must stay exactly zero.
double group_error(std::span<const Complex> device, std::span<const Complex> native) {
    double difference = 0, norm = 0;
    for (std::size_t i = 0; i < native.size(); ++i) {
        difference += std::norm(device[i] - native[i]);
        norm += std::norm(native[i]);
    }
    if (norm == 0) return difference == 0 ? 0. : std::numeric_limits<double>::infinity();
    return std::sqrt(difference / norm);
}
std::array<Complex, 18> affine_values(const r::FineBulkStatistics& s) {
    std::array<Complex, 18> values{};
    std::ranges::copy(s.correlations, values.begin());
    std::ranges::copy(s.gram, values.begin() + 6);
    return values;
}
std::array<Complex, 5> cell_values(const r::VaryingIndependentRow& cell) {
    return {cell.correlations[0], cell.correlations[1], Complex(cell.diagonal[0]), Complex(cell.diagonal[1]), cell.cross};
}
// The second Cholesky pivot of a two-tap Gram, relative to its echo energy.
double second_pivot(const r::FineBulkStatistics& s, std::size_t hypothesis) {
    const auto start = 4 * hypothesis;
    const auto g0 = s.gram[start].real(), g1 = s.gram[start + 3].real();
    return (g1 - std::norm(s.gram[start + 1]) / g0) / g1;
}

struct FixtureResult {
    double affine_error = 0, independent_error = 0, rank_one_pivot = 0, fragile_pivot = 0, fragile_pivot_difference = 0;
    std::size_t calls = 0, admitted = 0, affine_groups = 0, cells = 0, zero_groups = 0, nonzero_cells = 0;
};

// Fills one block's templates: item 0 zero, item 1 rank one (each echo a
// multiple of its direct column), item 2 a fragile Gram (the echo nearly
// such a multiple), the rest random; the same for the tracked atoms.
void fill_templates(const Fixture& fixture, std::size_t rows, std::span<Complex> out, Values& values) {
    std::size_t column = 0;
    const auto fill = [&](std::size_t columns, std::size_t taps, std::size_t pattern) {
        for (std::size_t c = 0; c < columns; ++c) {
            auto target = out.subspan((column + c) * rows, rows);
            const bool echo = taps == 2 && c % 2 == 1;
            for (std::size_t i = 0; i < rows; ++i) {
                if (pattern == 0) target[i] = {};
                else if (echo && pattern == 1) target[i] = Complex(.75, -.5) * out[(column + c - 1) * rows + i];
                else if (echo && pattern == 2)
                    target[i] = Complex(-.25, .6) * out[(column + c - 1) * rows + i] + 1e-7 * values.complex();
                else target[i] = values.complex();
            }
        }
        column += columns;
    };
    for (std::size_t item = 0; item < fixture.taps.size(); ++item)
        fill(2 * fixture.taps[item], fixture.taps[item], item < 3 ? item : 3);
    for (std::size_t atom = 0; atom < fixture.atoms; ++atom) fill(4, 2, atom < 2 ? atom : 3);
}

FixtureResult run_fixture(const Fixture& fixture, std::stop_token stop) {
    const std::array<Length, 2> lengths{Length(fixture.block_rows, fixture.modes[0], fixture.states, fixture.block_rows),
                                        Length(fixture.final_rows, fixture.modes[1], fixture.states, fixture.block_rows)};
    const auto shape = shape_of(fixture, lengths);
    VulkanReceiverOptions options{0, workspace};
    options.varying_fine = VulkanStageRoute::device;
    const auto backend = make_vulkan_receiver_backend(options);
    check(!backend->receipt().precision.ends_with("_fixed_phase_host_fp64_scoring"),
          "Vulkan varying device checks need FP64/INT64 shaders; this device declines as " +
              text(StageReason::shader_fp64_unavailable));
    check(backend->stage_decline(shape, false).empty(), fixture.name + ": the explicit device route declined");
    auto session = backend->make_varying(shape, false, stop);
    check(session && session->batch_blocks() == fixture.batch, fixture.name + ": the device session was not admitted");
    const auto layout = r::varying_staging_layout(shape, fixture.batch);
    check(session->template_staging(0).size() == layout.bytes && session->template_staging(1).size() == layout.bytes &&
          session->template_staging(2).empty(), fixture.name + ": staging slots differ from their layout");
    const auto unified = backend->receipt().unified_memory;
    const auto host = backend->stage_host_bytes(shape, false);
    const auto leased = backend->receipt().vulkan_leased_bytes;
    // Every device quotes the session's compilation to the preflight; on a
    // unified-memory device the lease is host RAM as well, quoted in full,
    // and on another device at most its host-read buffers.
    check(leased > 0 && host >= vk::compiler_host_bound &&
              (unified ? host - vk::compiler_host_bound == leased : host - vk::compiler_host_bound < leased),
          fixture.name + ": the session's host quote differs");
    check(backend->receipt().reserved_bytes <= leased, fixture.name + ": the session allocated beyond its lease");
    // The stage constants, in slot 0.
    auto constants = session->template_staging(0);
    auto* taps = reinterpret_cast<std::uint32_t*>(constants.data() + layout.taps_offset);
    std::ranges::copy(fixture.taps, taps);
    auto* shifts = reinterpret_cast<std::int64_t*>(constants.data() + layout.shifts_offset);
    for (std::size_t length = 0; length < 2; ++length)
        std::ranges::copy(lengths[length].bins, shifts + length * fixture.states);
    const std::span<const std::uint32_t> taps_span(taps, fixture.taps.size());
    const std::span<const std::int64_t> shift_span(shifts, 2 * fixture.states);

    FixtureResult result;
    result.admitted = session->batch_blocks();
    Values values{0x5eed0000ULL + fixture.block_rows};
    const double variance = .37;
    const auto stride = fixture.batch;
    const auto states = fixture.taps.size() - fixture.center_atoms, cells = 2 * fixture.states;
    std::uint64_t score_rows = 0;
    for (std::size_t first = 0; first < fixture.blocks; first += fixture.batch) {
        const auto count = std::min(fixture.batch, fixture.blocks - first);
        const auto slot = result.calls % 2;
        auto staging = session->template_staging(slot);
        auto* templates = reinterpret_cast<Complex*>(staging.data());
        auto* projected = reinterpret_cast<Complex*>(staging.data() + layout.projected_offset);
        std::vector<BlockReference> expected;
        for (std::size_t block = 0; block < count; ++block) {
            const auto terminal = first + block + 1 == fixture.blocks;
            const auto& length = lengths[terminal ? 1 : 0];
            auto out = std::span(templates + block * layout.block_values, layout.template_columns * length.rows);
            fill_templates(fixture, length.rows, out, values);
            auto y = std::span(projected + block * layout.mode_stride, length.indices.size());
            for (auto& value : y) value = values.complex();
            expected.push_back(reference(fixture, length, out, y, variance));
        }
        r::VaryingBatch batch;
        batch.first_block = first; batch.blocks = count; batch.terminal_last = first + count == fixture.blocks;
        batch.tables = {lengths[0].tables(), lengths[1].tables()};
        batch.projected_y = {projected, count * layout.mode_stride};
        batch.independent_shifts = shift_span; batch.staging_slot = slot;
        batch.affine_taps = taps_span; batch.variance = variance;
        std::vector<r::FineBulkStatistics> affine(states * stride), center(fixture.center_atoms * stride);
        std::vector<r::VaryingIndependentRow> independent(fixture.atoms * stride * cells);
        r::VaryingBatchOutput output{affine, independent, center, stride};
        r::StageDeviceWork work;
        check(session->statistics(batch, output, work, stop), fixture.name + ": a finite batch fell back natively");
        check(work.forward_ffts && work.score_rows == count * (fixture.taps.size() + fixture.atoms * cells),
              fixture.name + ": the call's device work is wrong");
        score_rows += work.score_rows;
        for (std::size_t block = 0; block < count; ++block) {
            const auto& native = expected[block];
            for (std::size_t item = 0; item < fixture.taps.size(); ++item) {
                const auto& device = item < states ? affine[item * stride + block]
                                                   : center[(item - states) * stride + block];
                const auto a = affine_values(device), b = affine_values(native.affine[item]);
                const auto error = group_error(a, b);
                check(!device.nonzero_basis && std::isfinite(error),
                      fixture.name + ": an affine group differs from the native statistics");
                result.affine_error = std::max(result.affine_error, error);
                result.zero_groups += item == 0;
                ++result.affine_groups;
                if (fixture.taps[item] == 2 && (item == 1 || item == 2))
                    for (std::size_t hypothesis = 1; hypothesis <= 2; ++hypothesis) {
                        const auto pivot = second_pivot(native.affine[item], hypothesis);
                        if (item == 1) result.rank_one_pivot = std::max(result.rank_one_pivot, std::abs(pivot));
                        else {
                            result.fragile_pivot = std::max(result.fragile_pivot, std::abs(pivot));
                            result.fragile_pivot_difference = std::max(result.fragile_pivot_difference,
                                std::abs(second_pivot(device, hypothesis) - pivot) / std::abs(pivot));
                        }
                    }
            }
            for (std::size_t atom = 0; atom < fixture.atoms; ++atom)
                for (std::size_t cell = 0; cell < cells; ++cell) {
                    const auto& device = independent[(atom * stride + block) * cells + cell];
                    const auto& native = expected[block].cells[atom * cells + cell];
                    const auto error = group_error(cell_values(device), cell_values(native));
                    check(device.half_bin == native.half_bin && device.bit == native.bit &&
                          device.state == native.state && std::isfinite(error),
                          fixture.name + ": a tracked cell differs from the native visitor");
                    result.independent_error = std::max(result.independent_error, error);
                    result.zero_groups += atom == 0;
                    result.nonzero_cells += std::ranges::any_of(cell_values(native),
                        [](Complex value) { return value != Complex{}; });
                    ++result.cells;
                }
        }
        ++result.calls;
    }
    check(result.affine_error <= statistic_bound && result.independent_error <= statistic_bound,
          fixture.name + ": device statistics exceed the predeclared FP64 bound");
    // Atoms after the zero one carry nonzero cells, so their bound is not vacuous.
    check(fixture.atoms < 2 || result.nonzero_cells >= (fixture.atoms - 1) * fixture.blocks * cells,
          fixture.name + ": the tracked atoms' reference cells are unexpectedly zero");
    const auto receipt = stage(*backend);
    const auto total = backend->receipt();
    check(receipt.planned_calls == 1 && receipt.sessions == 1 && !receipt.declined_calls && !receipt.native_fallbacks &&
          receipt.score_rows == score_rows && receipt.forward_ffts && receipt.fft_row_stages && receipt.upload_bytes &&
          total.vulkan_varying_blocks == fixture.blocks && total.vulkan_varying_batches == result.calls &&
          !total.fft_calls && !total.vulkan_dispatches && !total.synchronizations,
          fixture.name + ": the session receipt is wrong");
    session.reset();
    check(backend->receipt().vulkan_leased_bytes == 0 && backend->receipt().reserved_bytes == 0,
          fixture.name + ": the session kept its lease");
    return result;
}

// Fallbacks of an admitted session's call, and cancellation: no output or
// work counter changes.
void check_session_fallbacks(std::stop_token stop) {
    const Fixture fixture{"fallback", 200, 239, {27, 33}, {2, 2, 2, 2}, 0, 1, 9, 2, 2};
    const std::array<Length, 2> lengths{Length(200, 27, 9, 200), Length(239, 33, 9, 200)};
    const auto shape = shape_of(fixture, lengths);
    const auto layout = r::varying_staging_layout(shape, 2);
    // A fresh session whose slot 0 holds a finite two-block batch.
    struct Session {
        std::shared_ptr<r::ReceiverBulkBackend> backend;
        std::unique_ptr<r::VaryingBulk> bulk;
        Complex* templates = nullptr;
        Complex* projected = nullptr;
        r::VaryingBatch batch;
    };
    const auto open = [&] {
        Session result;
        VulkanReceiverOptions options{0, workspace};
        options.varying_fine = VulkanStageRoute::device;
        result.backend = make_vulkan_receiver_backend(options);
        result.bulk = result.backend->make_varying(shape, false, stop);
        check(static_cast<bool>(result.bulk), "fallback fixture was not admitted");
        auto staging = result.bulk->template_staging(0);
        auto* taps = reinterpret_cast<std::uint32_t*>(staging.data() + layout.taps_offset);
        std::ranges::copy(fixture.taps, taps);
        auto* shifts = reinterpret_cast<std::int64_t*>(staging.data() + layout.shifts_offset);
        for (std::size_t length = 0; length < 2; ++length) std::ranges::copy(lengths[length].bins, shifts + 9 * length);
        Values values{77};
        result.templates = reinterpret_cast<Complex*>(staging.data());
        result.projected = reinterpret_cast<Complex*>(staging.data() + layout.projected_offset);
        for (std::size_t block = 0; block < 2; ++block) {
            const auto rows = lengths[block].rows;
            fill_templates(fixture, rows, std::span(result.templates + block * layout.block_values,
                                                    layout.template_columns * rows), values);
            for (std::size_t j = 0; j < lengths[block].indices.size(); ++j)
                result.projected[block * layout.mode_stride + j] = values.complex();
        }
        auto& batch = result.batch;
        batch.blocks = 2; batch.terminal_last = true;
        batch.tables = {lengths[0].tables(), lengths[1].tables()};
        batch.projected_y = {result.projected, 2 * layout.mode_stride};
        batch.independent_shifts = {shifts, 18}; batch.affine_taps = {taps, 4}; batch.variance = 1;
        return result;
    };
    r::FineBulkStatistics sentinel;
    sentinel.correlations[0] = {12345, 0};
    std::vector<r::FineBulkStatistics> affine(4 * 2, sentinel);
    std::vector<r::VaryingIndependentRow> independent(2 * 18);
    for (auto& cell : independent) cell.half_bin = 9999;
    r::VaryingBatchOutput output{affine, independent, {}, 2};
    const auto untouched = [&] {
        return std::ranges::all_of(affine, [](const auto& s) { return s.correlations[0] == Complex(12345, 0); }) &&
               std::ranges::all_of(independent, [](const auto& c) { return c.half_bin == 9999; });
    };
    const auto device_work = [](const r::ReceiverBulkBackend& backend) {
        const auto s = stage(backend);
        return std::tuple(s.sessions, s.forward_ffts, s.inverse_ffts, s.score_rows, s.upload_bytes,
                          backend.receipt().vulkan_varying_blocks);
    };
    // A malformed call is a receiver defect, never a fallback, and an
    // unstopped finite call publishes.
    {
        auto session = open();
        r::StageDeviceWork work;
        auto wrong = session.batch;
        wrong.blocks = 3;
        bool rejected = false;
        try { static_cast<void>(session.bulk->statistics(wrong, output, work, stop)); }
        catch (const Error&) { rejected = true; }
        check(rejected && untouched() && !stage(*session.backend).native_fallbacks,
              "a batch beyond the admitted session must be rejected");
        // Cancellation throws before any device work.
        std::stop_source cancel;
        cancel.request_stop();
        const auto before = device_work(*session.backend);
        bool threw = false;
        try { static_cast<void>(session.bulk->statistics(session.batch, output, work, cancel.get_token())); }
        catch (const Error&) { threw = true; }
        check(threw && untouched() && device_work(*session.backend) == before && work == r::StageDeviceWork{} &&
              !stage(*session.backend).native_fallbacks, "a cancelled varying call changed outputs or counters");
        check(session.bulk->statistics(session.batch, output, work, stop) && !untouched() &&
              stage(*session.backend).sessions == 1, "a varying session must publish after a cancelled call");
        session.bulk.reset();
        check(session.backend->receipt().vulkan_leased_bytes == 0, "a cancelled session kept its lease");
    }
    // A nonfinite projected observation keeps the batch native before any
    // device work; a nonfinite template yields nonfinite device statistics.
    for (const auto reason : {StageReason::nonfinite_input, StageReason::nonfinite_device}) {
        std::ranges::fill(affine, sentinel);
        for (auto& cell : independent) cell.half_bin = 9999;
        auto session = open();
        const auto before = device_work(*session.backend);
        auto& value = reason == StageReason::nonfinite_input ? session.projected[3] : session.templates[5 * 200 + 7];
        const auto saved = value;
        value = {reason == StageReason::nonfinite_input ? std::numeric_limits<double>::quiet_NaN() :
                 std::numeric_limits<double>::infinity(), 0};
        r::StageDeviceWork work;
        check(!session.bulk->statistics(session.batch, output, work, stop) && untouched() &&
              device_work(*session.backend) == before && work == r::StageDeviceWork{} &&
              stage(*session.backend).native_fallbacks == 1 && stage(*session.backend).planned_calls == 1 &&
              stage(*session.backend).fallback_reason == text(reason),
              "a nonfinite varying batch must fall back natively, untouched");
        value = saved;
        check(session.bulk->statistics(session.batch, output, work, stop) && !untouched() &&
              stage(*session.backend).sessions == 1 && stage(*session.backend).native_fallbacks == 1,
              "a varying session must publish after a fallback");
        // The planned-call partition holds with a session's fallbacks.
        const auto s = stage(*session.backend);
        check(s.declined_calls + s.sessions <= s.planned_calls &&
              s.planned_calls <= s.declined_calls + s.sessions + s.native_fallbacks,
              "varying receipt breaks its planned-call partition");
    }
}

// Workspace leases: refused below one block, halved from the widest batch,
// and bounded across concurrent owners by the shared workspace, which a
// lease that never waits refuses once it is held.
std::array<std::size_t, 3> check_leases(std::stop_token stop) {
    const Fixture fixture{"lease", 200, 239, {27, 33}, std::vector<std::uint32_t>(54, 2), 0, 4, 27, 1024, 1};
    const std::array<Length, 2> lengths{Length(200, 27, 27, 200), Length(239, 33, 27, 200)};
    const auto shape = shape_of(fixture, lengths);
    {
        VulkanReceiverOptions options{0, 1024 * 1024};
        options.varying_fine = VulkanStageRoute::device;
        const auto backend = make_vulkan_receiver_backend(options);
        check(!backend->make_varying(shape, false, stop) && stage(*backend).planned_calls == 1 &&
              stage(*backend).native_fallbacks == 1 && !stage(*backend).sessions &&
              stage(*backend).fallback_reason == text(StageReason::workspace) &&
              backend->receipt().vulkan_leased_bytes == 0 && backend->stage_host_bytes(shape, false) == 0,
              "a lease beyond one block must fall back to the native stage");
    }
    constexpr std::size_t limit = 96 * 1024 * 1024;
    VulkanReceiverOptions options{0, limit};
    options.varying_fine = VulkanStageRoute::device;
    const auto backend = make_vulkan_receiver_backend(options);
    std::vector<std::unique_ptr<r::VaryingBulk>> sessions;
    std::size_t previous = shape.batch_blocks;
    while (auto session = backend->make_varying(shape, false, stop)) {
        const auto blocks = session->batch_blocks();
        check(blocks <= previous && std::has_single_bit(blocks) && backend->receipt().vulkan_leased_bytes <= limit &&
              sessions.size() < 64, "concurrent varying sessions exceeded the shared workspace");
        if (sessions.empty()) {
            const auto host = backend->stage_host_bytes(shape, false);
            const auto leased = backend->receipt().vulkan_leased_bytes;
            check(host >= vk::compiler_host_bound && (backend->receipt().unified_memory
                      ? host - vk::compiler_host_bound == leased : host - vk::compiler_host_bound < leased),
                  "the host quote differs from the compilation and the admitted lease");
        }
        previous = blocks;
        sessions.push_back(std::move(session));
    }
    check(sessions.size() >= 2 && sessions.front()->batch_blocks() < shape.batch_blocks &&
          stage(*backend).fallback_reason == text(StageReason::workspace) && stage(*backend).native_fallbacks == 1 &&
          stage(*backend).planned_calls == sessions.size() + 1,
          "a held workspace must refuse a lease that never waits");
    const std::array<std::size_t, 3> admitted{sessions.front()->batch_blocks(), sessions.size(),
                                              sessions.back()->batch_blocks()};
    sessions.clear();
    check(backend->receipt().vulkan_leased_bytes == 0 && backend->receipt().reserved_bytes == 0,
          "varying sessions kept their leases");
    return admitted;
}

// The selection policy on the backend, and the receiver's own records.
void check_policy(std::stop_token stop) {
    const Fixture fixture{"policy", 200, 239, {27, 33}, {2, 2}, 0, 1, 9, 2, 2};
    const std::array<Length, 2> lengths{Length(200, 27, 9, 200), Length(239, 33, 9, 200)};
    const auto shape = shape_of(fixture, lengths);
    auto unsupported = shape;
    unsupported.bluestein_rows[1] = 256;
    auto fp32 = shape;
    fp32.arithmetic = r::VaryingArithmetic::fp32_template;
    for (const auto route : {VulkanStageRoute::device, VulkanStageRoute::automatic, VulkanStageRoute::native}) {
        VulkanReceiverOptions options{0, workspace};
        options.varying_fine = route;
        const auto backend = make_vulkan_receiver_backend(options);
        const bool host_scoring = backend->receipt().precision.ends_with("_fixed_phase_host_fp64_scoring");
        const auto expected = route == VulkanStageRoute::native ? text(StageReason::native_requested)
            : host_scoring ? text(StageReason::shader_fp64_unavailable)
            : route == VulkanStageRoute::automatic ? text(StageReason::unqualified_device) : std::string{};
        check(!stage(*backend).qualified_device && backend->stage_decline(shape, false) == expected &&
              backend->stage_decline(r::StageShape{StageFamily::varying}, false) == expected,
              "varying route declines with the wrong reason");
        const auto shape_reason = route == VulkanStageRoute::native ? text(StageReason::native_requested)
            : host_scoring ? text(StageReason::shader_fp64_unavailable) : text(StageReason::unsupported_shape);
        check(backend->stage_decline(unsupported, false) == shape_reason &&
              backend->stage_decline(fp32, false) == shape_reason &&
              backend->stage_decline(shape, true) == (route == VulkanStageRoute::native ?
                  text(StageReason::native_requested) : text(StageReason::fp64_requested)),
              "varying policy order differs on the backend");
        auto session = backend->make_varying(shape, false, stop);
        check(static_cast<bool>(session) == expected.empty() && stage(*backend).planned_calls == 1 &&
              stage(*backend).declined_calls == !expected.empty() && stage(*backend).decline_reason == expected,
              "the varying factory must admit exactly the accepted route (" + std::string(session ? "session" : "none") +
              ", planned " + std::to_string(stage(*backend).planned_calls) + ", declined " +
              std::to_string(stage(*backend).declined_calls) + ", decline '" + stage(*backend).decline_reason +
              "', fallback '" + stage(*backend).fallback_reason + "')");
        if (!expected.empty())
            check(backend->stage_host_bytes(shape, false) == 0, "a declined varying stage must quote no lease");
        session.reset();
        // The receiver's own declines and fallbacks are recorded as planned
        // calls of this family.
        for (const auto reason : {StageReason::diagnostic, StageReason::line, StageReason::coherent_gain,
                                  StageReason::projected_tracker})
            backend->record_stage_native(StageFamily::varying, reason, StageNative::decline);
        backend->record_stage_native(StageFamily::varying, StageReason::unified_memory_envelope, StageNative::fallback);
        const auto s = stage(*backend);
        check(s.planned_calls == 6 && s.declined_calls == 4 + !expected.empty() && s.native_fallbacks == 1 &&
              s.fallback_reason == text(StageReason::unified_memory_envelope) &&
              s.decline_reason == (expected.empty() ? text(StageReason::diagnostic) : expected),
              "the receiver's varying records are wrong");
        // The application wrapper reaches the same factory.
        const auto wrapped = r::make_selected_gpu_backend(backend, "varying check");
        check(wrapped->stage_decline(shape, false) == expected &&
              static_cast<bool>(wrapped->make_varying(shape, false, stop)) == expected.empty(),
              "the GPU selection wrapper does not reach the Vulkan varying factory");
    }
    // FP32 templates have no device kernels; forced host scoring declines.
    VulkanReceiverOptions options{0, workspace};
    options.varying_fine = VulkanStageRoute::device;
    options.varying_arithmetic = VulkanVaryingArithmetic::fp32_template;
    check(make_vulkan_receiver_backend(options)->stage_decline(shape, false) == text(StageReason::unimplemented),
          "the FP32-template arithmetic must decline as unimplemented");
    options.varying_arithmetic = VulkanVaryingArithmetic::automatic;
    options.force_host_scoring = true;
    const auto forced = make_vulkan_receiver_backend(options);
    check(forced->stage_decline(unsupported, false) == text(StageReason::host_scoring_forced) &&
          !forced->make_varying(shape, false, stop) &&
          stage(*forced).decline_reason == text(StageReason::host_scoring_forced),
          "forced host scoring must decline the varying stage");
}

// ---- The receiver's device route ----------------------------------------

constexpr double receiver_evidence_bound = 1e-8;
std::string number(double);

// Forwards every query and factory to the Vulkan backend. Its varying
// sessions make chosen calls fall back natively: a NaN in the call's first
// projected mode, which the real session rejects before any device work.
class ForcingBackend final : public r::ReceiverBulkBackend {
public:
    enum class Mode { none, all, odd };
    ForcingBackend(std::shared_ptr<r::ReceiverBulkBackend> backend, Mode mode)
        : backend_(std::move(backend)), mode_(mode) {}
    std::size_t calls = 0, forced = 0;

    bool supports_fine() const noexcept override { return backend_->supports_fine(); }
    std::string_view fine_fallback_reason(const r::FineBulkGeometry& geometry) const noexcept override {
        return backend_->fine_fallback_reason(geometry);
    }
    bool fine_maps_on_host() const noexcept override { return backend_->fine_maps_on_host(); }
    std::size_t fine_resident_blocks(const r::FineBulkGeometry& geometry) const noexcept override {
        return backend_->fine_resident_blocks(geometry);
    }
    bool permits_native_exact_chirp_z() const noexcept override { return backend_->permits_native_exact_chirp_z(); }
    bool supports_exact_device_chirp_z() const noexcept override { return backend_->supports_exact_device_chirp_z(); }
    std::size_t maximum_frequency_tile_width(const r::CoarseBulkGeometry& geometry, std::size_t workspaces,
                                             std::size_t width, double clock) const override {
        return backend_->maximum_frequency_tile_width(geometry, workspaces, width, clock);
    }
    std::optional<r::CoarseFrequencyPlan> frequency_plan(const r::CoarseBulkGeometry& geometry, std::size_t workspaces,
                                                         std::size_t rows) const override {
        return backend_->frequency_plan(geometry, workspaces, rows);
    }
    std::string_view coarse_fallback_reason(double rate, double clock, r::CoarseRoute route) const noexcept override {
        return backend_->coarse_fallback_reason(rate, clock, route);
    }
    void record_native_coarse(std::string_view reason) override { backend_->record_native_coarse(reason); }
    void record_native_fine(std::string_view reason) override { backend_->record_native_fine(reason); }
    std::unique_ptr<r::FineBulk> make_fine(double rate, double clock) override { return backend_->make_fine(rate, clock); }
    std::unique_ptr<r::CoarseBulk> make_coarse(double rate, double clock) override {
        return backend_->make_coarse(rate, clock);
    }
    std::size_t persistent_host_workspace() const noexcept override { return backend_->persistent_host_workspace(); }
    std::size_t segmented_host_bytes(std::span<const r::CoarseSegmentedShape> shapes) const override {
        return backend_->segmented_host_bytes(shapes);
    }
    std::size_t fine_host_bytes(std::size_t rows, std::size_t taps) const override {
        return backend_->fine_host_bytes(rows, taps);
    }
    std::size_t coarse_host_bytes(std::size_t rows, std::size_t frequencies, std::size_t arrivals) const override {
        return backend_->coarse_host_bytes(rows, frequencies, arrivals);
    }
    r::BulkReceipt receipt() const override { return backend_->receipt(); }
    r::BulkReceipt status_receipt() const override { return backend_->status_receipt(); }
    std::string_view stage_decline(const r::StageShape& shape, bool fp64_forced) const noexcept override {
        return backend_->stage_decline(shape, fp64_forced);
    }
    std::unique_ptr<r::GainRankBulk> make_gain_rank(const r::GainRankShape& shape, bool fp64_forced,
                                                    std::stop_token stop) override {
        return backend_->make_gain_rank(shape, fp64_forced, stop);
    }
    std::unique_ptr<r::DriftBulk> make_drift(const r::DriftBulkShape& shape, bool fp64_forced,
                                             std::stop_token stop) override {
        return backend_->make_drift(shape, fp64_forced, stop);
    }
    std::unique_ptr<r::VaryingBulk> make_varying(const r::VaryingBulkShape& shape, bool fp64_forced,
                                                 std::stop_token stop) override {
        auto session = backend_->make_varying(shape, fp64_forced, stop);
        if (!session) return {};
        return std::make_unique<Session>(std::move(session), *this);
    }
    std::size_t stage_host_bytes(const r::StageShape& shape, bool fp64_forced) const override {
        return backend_->stage_host_bytes(shape, fp64_forced);
    }
    void record_stage_native(StageFamily family, StageReason reason, StageNative kind) override {
        backend_->record_stage_native(family, reason, kind);
    }

private:
    class Session final : public r::VaryingBulk {
    public:
        Session(std::unique_ptr<r::VaryingBulk> inner, ForcingBackend& owner)
            : inner_(std::move(inner)), owner_(owner) {}
        std::span<std::byte> template_staging(std::size_t slot) noexcept override {
            return inner_->template_staging(slot);
        }
        std::size_t batch_blocks() const noexcept override { return inner_->batch_blocks(); }
        r::BulkReceipt receipt() const override { return inner_->receipt(); }
        bool statistics(const r::VaryingBatch& batch, r::VaryingBatchOutput& output, r::StageDeviceWork& work,
                        std::stop_token stop) override {
            const auto call = owner_.calls++;
            if (owner_.mode_ == Mode::none || (owner_.mode_ == Mode::odd && call % 2 == 0))
                return inner_->statistics(batch, output, work, stop);
            ++owner_.forced;
            auto staging = inner_->template_staging(batch.staging_slot);
            const auto offset = reinterpret_cast<const std::byte*>(batch.projected_y.data()) - staging.data();
            auto& first_mode = *reinterpret_cast<Complex*>(staging.data() + offset);
            const auto saved = first_mode;
            first_mode = {std::numeric_limits<double>::quiet_NaN(), 0};
            const auto published = inner_->statistics(batch, output, work, stop);
            first_mode = saved;
            check(!published, "a forced varying call published device statistics");
            return false;
        }
    private:
        std::unique_ptr<r::VaryingBulk> inner_;
        ForcingBackend& owner_;
    };
    std::shared_ptr<r::ReceiverBulkBackend> backend_;
    Mode mode_;
};

// The public fine fixture of test_triad_fine_session: a rendered DATA
// pattern of one bit in a 19-second slot.
struct ReceiverFixture {
    std::string name;
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    Crypto key = Crypto::amateur();
    std::vector<Complex> iq;
    std::vector<r::FineCoordinate> coordinates;
    ReceiverFixture(std::string label, unsigned fs) : name(std::move(label)) {
        config.sample_rate_hz = fs;
        config.data_if_hz = 1725;
        config.noise_variance = .05;
        slot.index = 17;
        slot.first_sample = 90'000'000'000ULL;
        slot.stop_sample = slot.first_sample + 19 * fs + 17;
        slot.quiet_first_sample = slot.first_sample + 2 * fs;
        slot.quiet_stop_sample = slot.stop_sample - 2 * fs;
        slot.format.sample_rate = fs;
        slot.format.inner_rate_hz = 120;
        slot.format.added_symbols = 1;
        slot.format.bit_seconds = .001;
        slot.format.pulse_shape = PulseShape::TaperedRrc;
        slot.format.epoch = 1793850000;
        slot.format.symbol_ordinal = 3117;
        slot.window_capture_lead_seconds = -17;
        slot.source_start_seconds = 18;
        PatternSource source(slot.format, key, 1);
        iq.resize(slot.stop_sample - slot.first_sample);
        check(source.geometry().source_samples < iq.size() - fs, "receiver varying fixture does not contain DATA support");
        source.render(0, std::span(iq).subspan(fs, source.geometry().source_samples));
        // Two nominal independent atoms at the public arrival endpoints, at
        // the central clock.
        config.time_varying_channel = true;
        config.spread_delay_seconds = .004;
        config.clock_bound_ppm = 0;
        slot.arrival_lower_seconds = -.004;
        slot.arrival_upper_seconds = .004;
        coordinates = {{0, 0, 0, 0}, {2, .001, 0, 0}, {-2, -.001, 0, 0}};
    }
};

// Every evidence term of the varying stage, in a fixed order, and the
// integer receipts and statuses that must not depend on the route.
std::vector<double> evidence_terms(const ReceiverSlotScore& r) {
    std::vector<double> out;
    for (const auto& pair : {r.varying_bit_evidence, r.fixed_varying_bit_evidence, r.tracked_frequency_bit_evidence,
             r.nominal_independent_fixed_centroid_bit_evidence, r.nominal_independent_moving_centroid_bit_evidence,
             r.nominal_independent_gain_bit_evidence, r.signed_lag_gain_bit_evidence})
        out.insert(out.end(), pair.begin(), pair.end());
    for (const auto& pair : r.nominal_independent_gain_power_bit_evidence) out.insert(out.end(), pair.begin(), pair.end());
    for (const auto& pair : r.signed_lag_gain_map_bit_evidence) out.insert(out.end(), pair.begin(), pair.end());
    out.push_back(r.tracked_frequency_log_prior_adjustment);
    out.push_back(r.nominal_independent_gain_log_prior_adjustment);
    return out;
}
std::string receiver_counters(const ReceiverSlotScore& r) {
    std::string out = r.varying_status + ',' + r.nominal_independent_gain_frequency_status + ',';
    for (const auto value : {std::uint64_t{r.varying_candidates}, std::uint64_t{r.varying_blocks},
             std::uint64_t{r.varying_block_rows}, r.varying_basis_rows, std::uint64_t{r.varying_frequency_states},
             std::uint64_t{r.midpoint_timing_candidates}, r.midpoint_timing_basis_rows,
             r.nominal_independent_gain_basis_rows, r.signed_lag_gain_basis_rows, r.independent_gain_emission_cells,
             std::uint64_t{r.nominal_independent_gain_candidates}, std::uint64_t{r.signed_lag_gain_candidates},
             std::uint64_t{r.nominal_independent_gain_frequency_states}, std::uint64_t{r.varying_workspace_bytes},
             std::uint64_t{r.varying_batch_blocks}, std::uint64_t{r.varying_batches}})
        out += std::to_string(value) + ',';
    return out;
}
// The largest |difference| of two runs' evidence terms (an absent family's
// -inf must match exactly); infinite when a term's bit decision (the sign of
// its bit-one minus bit-zero term) differs.
double evidence_delta(const ReceiverSlotScore& a, const ReceiverSlotScore& b) {
    const auto x = evidence_terms(a), y = evidence_terms(b);
    double delta = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i] == y[i]) continue;
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) return std::numeric_limits<double>::infinity();
        delta = std::max(delta, std::abs(x[i] - y[i]));
    }
    // Bit pairs: every term but the two trailing prior adjustments. A pair
    // whose native margin exceeds twice the bound must keep its decision.
    for (std::size_t i = 0; i + 1 < x.size() - 2; i += 2)
        if (std::abs(y[i + 1] - y[i]) > 2 * receiver_evidence_bound && (x[i + 1] > x[i]) != (y[i + 1] > y[i]))
            return std::numeric_limits<double>::infinity();
    return delta;
}
bool bit_identical(const ReceiverSlotScore& a, const ReceiverSlotScore& b) {
    const auto x = evidence_terms(a), y = evidence_terms(b);
    return std::ranges::equal(x, y, [](double p, double q) {
        return std::bit_cast<std::uint64_t>(p) == std::bit_cast<std::uint64_t>(q);
    }) && receiver_counters(a) == receiver_counters(b);
}

struct ReceiverRouteResult {
    std::size_t blocks = 0, block_rows = 0, batches = 0, admitted = 0, tracked_states = 0;
    double device_delta = 0, partly_delta = 0;
};

ReceiverRouteResult check_receiver_route(ReceiverFixture& fixture, bool tracked, std::stop_token stop) {
    const auto& name = fixture.name;
    auto plan = r::varying_fine_plan(fixture.config, fixture.slot, 2);
    const auto total = fixture.iq.size() / std::max<std::size_t>(1, plan.block_rows);
    check(plan.block_rows && plan.track_frequency && plan.nominal_independent_gain_candidates == 2 && total >= 4,
          name + ": the receiver fixture must quote two nominal independent atoms over several blocks");
    // At least four batches, so alternating calls mix device and native.
    plan.batch_blocks = std::min(plan.batch_blocks, total / 4);
    const auto batches = (total + plan.batch_blocks - 1) / plan.batch_blocks;
    // The receiver's quote sets the envelope beside its own; here it holds
    // the compilation and the whole explicit lease.
    plan.device_envelope_bytes = vk::compiler_host_bound + workspace;
    const auto backend_for = [](VulkanStageRoute route) {
        VulkanReceiverOptions options{0, workspace};
        options.varying_fine = route;
        options.varying_arithmetic = VulkanVaryingArithmetic::fp64;
        return make_vulkan_receiver_backend(options);
    };
    r::ReceiverExecutor executor(2);
    const auto run = [&](std::shared_ptr<r::ReceiverBulkBackend> backend, const r::VaryingFinePlan& shape) {
        auto config = fixture.config;
        config.bulk_backend = std::move(backend);
        ReceiverSlotScore score;
        r::score_varying_fine(config, fixture.slot, fixture.key, shape, fixture.iq, fixture.coordinates, {}, 100, 12, 2,
                              score, stop, executor);
        return score;
    };
    const auto route_is = [](const ReceiverSlotScore& score, std::string_view route, std::string_view arithmetic) {
        return score.varying_fine_route == route && score.varying_fine_arithmetic == arithmetic;
    };
    // The native route, and the receiver without a backend.
    const auto native_backend = backend_for(VulkanStageRoute::native);
    const auto native = run(native_backend, plan);
    check(route_is(native, "native", "native_fp64") && stage(*native_backend).declined_calls == 1 &&
          stage(*native_backend).decline_reason == text(StageReason::native_requested) &&
          bit_identical(native, run(nullptr, plan)) && native.varying_batches == batches &&
          native.nominal_independent_gain_candidates == 2 &&
          (tracked ? native.nominal_independent_gain_frequency_states > 1
                   : native.nominal_independent_gain_frequency_states == 0),
          name + ": the native route differs from the receiver without a backend");
    ReceiverRouteResult result;
    result.blocks = native.varying_blocks;
    result.block_rows = native.varying_block_rows;
    result.batches = batches;
    result.tracked_states = native.nominal_independent_gain_frequency_states;
    const auto device_run = [&](ForcingBackend::Mode mode) {
        auto inner = backend_for(VulkanStageRoute::device);
        auto forcing = std::make_shared<ForcingBackend>(inner, mode);
        auto score = run(forcing, plan);
        const auto s = stage(*inner);
        const auto total_receipt = inner->receipt();
        check(forcing->calls == batches && s.planned_calls == 1 && !s.declined_calls &&
              s.native_fallbacks == forcing->forced && s.sessions == (forcing->forced < batches) &&
              total_receipt.vulkan_varying_batches == batches - forcing->forced &&
              total_receipt.vulkan_leased_bytes == 0 && total_receipt.reserved_bytes == 0 &&
              (!forcing->forced || s.fallback_reason == text(StageReason::nonfinite_input)),
              name + ": the device route's receipt differs from its calls");
        return std::pair{score, forcing->forced};
    };
    // (a) Every call falls back: the per-batch native reruns are the native route.
    const auto [fallen, all_forced] = device_run(ForcingBackend::Mode::all);
    check(all_forced == batches && route_is(fallen, "native", "native_fp64") && bit_identical(fallen, native),
          name + ": per-batch native reruns differ from the native route");
    // (b) Every call on the device.
    const auto [device, none_forced] = device_run(ForcingBackend::Mode::none);
    result.device_delta = evidence_delta(device, native);
    check(!none_forced && route_is(device, "vulkan", "vulkan_fp64_fft_fp64_state") &&
          receiver_counters(device) == receiver_counters(native) && result.device_delta <= receiver_evidence_bound,
          name + ": the device route changed a decision, a counter or evidence beyond 1e-8 (delta " +
          number(result.device_delta) + ", route " + device.varying_fine_route + ")");
    result.admitted = device.varying_batch_blocks;
    // (c) Alternating calls.
    const auto [partly, odd_forced] = device_run(ForcingBackend::Mode::odd);
    result.partly_delta = evidence_delta(partly, native);
    check(odd_forced == batches / 2 &&
          route_is(partly, "vulkan_partly_native", "vulkan_fp64_fft_fp64_state_partly_native_fp64") &&
          receiver_counters(partly) == receiver_counters(native) && result.partly_delta <= receiver_evidence_bound,
          name + ": a partly native device route differs from the native route");
    // (d) Host memory outside the preserved envelope keeps the stage native,
    // recorded as its fallback, before any factory call.
    auto narrow = plan;
    narrow.device_envelope_bytes = vk::compiler_host_bound - 1;
    const auto inner = backend_for(VulkanStageRoute::device);
    const auto outside = run(inner, narrow);
    const auto s = stage(*inner);
    check(route_is(outside, "native", "native_fp64") && bit_identical(outside, native) && s.planned_calls == 1 &&
          s.native_fallbacks == 1 && !s.sessions &&
          s.fallback_reason == text(inner->receipt().unified_memory ? StageReason::unified_memory_envelope
                                                                    : StageReason::workspace),
          name + ": a session outside the preserved envelope must fall back to the native stage");
    return result;
}

std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

} // namespace

void vulkan_varying_checks(std::stop_token stop) {
    check_policy(stop);
    // HF: 54 two-tap fixed-affine states, 4 atoms and 27 states on 200/239
    // rows (512-row transforms). 60 Hz: 48 states with one-tap models, 5
    // atoms and 75 states on 1200/1505 rows (4,096-row transforms), the final
    // block alone in its call. Power-of-two blocks beside a longer final
    // block with central atoms; tracked atoms on power-of-two blocks with no
    // final segment and beside a Bluestein final block; and a slot without a
    // remainder. Tracked atoms 1 and 2 are a rank-one and a random pair.
    std::vector<std::uint32_t> sixty;
    for (std::size_t i = 0; i < 48; ++i) sixty.push_back(i % 3 == 2 ? 1 : 2);
    const std::array fixtures{
        Fixture{"hf_200_239", 200, 239, {27, 33}, std::vector<std::uint32_t>(54, 2), 0, 4, 27, 3, 6},
        Fixture{"60hz_1200_1505", 1200, 1505, {115, 145}, sixty, 0, 5, 75, 2, 3},
        Fixture{"pow2_256_300_central", 256, 300, {20, 24}, {2, 1, 2, 2, 2, 2}, 2, 0, 0, 2, 3},
        Fixture{"pow2_256_256_tracked", 256, 256, {20, 20}, {2, 1, 2}, 0, 3, 13, 2, 3},
        Fixture{"pow2_256_300_tracked", 256, 300, {20, 24}, {2, 2}, 0, 3, 11, 2, 3},
        Fixture{"even_240", 240, 240, {29, 29}, {2, 2, 1}, 0, 3, 9, 2, 3}};
    std::vector<std::pair<std::string, FixtureResult>> results;
    for (const auto& fixture : fixtures) {
        check(!stop.stop_requested(), "Vulkan varying checks exceeded their deadline");
        results.emplace_back(fixture.name, run_fixture(fixture, stop));
    }
    check_session_fallbacks(stop);
    const auto leases = check_leases(stop);
    // The receiver's route: tracked atoms (12 Hz common Doppler at 480 Hz),
    // and central atoms, whose projected lattice is empty near Nyquist.
    ReceiverFixture tracked("receiver_tracked_480", 480);
    tracked.config.time_varying_common_doppler_hz = tracked.config.time_varying_atmospheric_hz = 12;
    tracked.config.spread_doppler_hz = 12;
    ReceiverFixture central("receiver_central_2400", 2400);
    central.config.frequency_bound_hz = central.config.time_varying_common_doppler_hz = 1161;
    std::vector<std::pair<std::string, ReceiverRouteResult>> routes;
    for (auto* fixture : {&tracked, &central}) {
        check(!stop.stop_requested(), "Vulkan varying checks exceeded their deadline");
        routes.emplace_back(fixture->name, check_receiver_route(*fixture, fixture == &tracked, stop));
    }
    std::cout << "{\"schema\":\"datapump.triad.vulkan-stage-checks.v1\",\"family\":\"varying\","
                 "\"status\":\"checks_passed\",\"actual_gpu_work\":true,"
                 "\"scope\":\"fp64_statistics_oracle_policy_receipts_leases_fallbacks_cancellation_receiver_route\","
                 "\"statistic_bound\":" << number(statistic_bound) << ",\"fixtures\":{";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& [name, r] = results[i];
        std::cout << (i ? "," : "") << '"' << name << "\":{\"calls\":" << r.calls << ",\"admitted_blocks\":" << r.admitted
                  << ",\"affine_groups\":" << r.affine_groups << ",\"cells\":" << r.cells
                  << ",\"zero_groups\":" << r.zero_groups << ",\"nonzero_cells\":" << r.nonzero_cells
                  << ",\"affine_max_norm_relative_error\":" << number(r.affine_error)
                  << ",\"independent_max_norm_relative_error\":" << number(r.independent_error)
                  << ",\"rank_one_relative_pivot\":" << number(r.rank_one_pivot)
                  << ",\"fragile_relative_pivot\":" << number(r.fragile_pivot)
                  << ",\"fragile_pivot_relative_difference\":" << number(r.fragile_pivot_difference) << '}';
    }
    std::cout << "},\"lease_first_admitted_blocks\":" << leases[0] << ",\"lease_concurrent_sessions\":" << leases[1]
              << ",\"lease_last_admitted_blocks\":" << leases[2] << ",\"receiver_evidence_bound\":"
              << number(receiver_evidence_bound) << ",\"receiver_routes\":{";
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const auto& [name, r] = routes[i];
        std::cout << (i ? "," : "") << '"' << name << "\":{\"blocks\":" << r.blocks << ",\"block_rows\":" << r.block_rows
                  << ",\"batches\":" << r.batches << ",\"batch_blocks\":" << r.admitted
                  << ",\"independent_frequency_states\":" << r.tracked_states
                  << ",\"device_max_abs_delta\":" << number(r.device_delta)
                  << ",\"partly_native_max_abs_delta\":" << number(r.partly_delta) << '}';
    }
    std::cout << "}}\n" << std::flush;
}

} // namespace datapump::triad::v3::test
