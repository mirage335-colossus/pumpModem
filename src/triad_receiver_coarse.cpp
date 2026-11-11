#include "triad_receiver_coarse.hpp"
#include "triad_receiver_executor.hpp"
#include "triad_receiver_detail.hpp"
#include "search_fft.hpp"
#include "datapump/execution.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace datapump::triad::v3::receiver_detail {
namespace {
namespace native_fft = modem::detail::search_fft;
constexpr std::size_t chunk_samples = 16384;
constexpr std::size_t lanes = 2 * receiver_epoch_offsets.size();
constexpr std::size_t retained_per_bit = 2;
constexpr std::size_t max_intervals = 64;
constexpr std::size_t max_frequency_points = 65536;
constexpr std::size_t max_lag_frequency_points = 4096;
constexpr std::size_t max_fft_products = 32768;
constexpr std::size_t max_integer_template_work = 30'000'000;
constexpr std::size_t max_mapped_template_work = 120'000'000;
constexpr double rank_cutoff = 1e-10;
constexpr double pi = std::numbers::pi;
constexpr auto infinity = std::numeric_limits<double>::infinity();

constexpr void require_coarse(bool condition, const char* reason) {
    if (!condition) throw Error(reason);
}

constexpr std::size_t add(std::size_t a, std::size_t b) {
    require_coarse(b <= SIZE_MAX - a, "coarse receiver extent overflow");
    return a + b;
}

constexpr std::size_t mul(std::size_t a, std::size_t b) {
    require_coarse(!a || b <= SIZE_MAX / a, "coarse receiver extent overflow");
    return a * b;
}

std::size_t extent(long double value) {
    require_coarse(std::isfinite(value) && value >= 0 && value < static_cast<long double>(SIZE_MAX),
                   "coarse receiver extent is outside the local address space");
    return static_cast<std::size_t>(value);
}

std::size_t fft_size(std::size_t rows) {
    require_coarse(rows && rows <= std::bit_floor(SIZE_MAX), "coarse FFT extent overflow");
    return std::bit_ceil(rows);
}

std::size_t ceil_div(std::size_t size, std::size_t step) {
    return size / step + (size % step != 0);
}

void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("coarse receiver cancelled");
    execution::checkpoint();
}

using PhaseClock = std::chrono::steady_clock;

double elapsed(PhaseClock::time_point began) {
    return std::chrono::duration<double>(PhaseClock::now() - began).count();
}

void note(std::uint64_t& target, std::uint64_t amount) {
    require_coarse(amount <= UINT64_MAX - target, "coarse operation count overflow");
    target += amount;
}

void combine(CoarseOperationCounts& total, const CoarseOperationCounts& part) {
    note(total.forward_ffts, part.forward_ffts);
    note(total.inverse_ffts, part.inverse_ffts);
    note(total.fft_rows, part.fft_rows);
    note(total.fft_row_stages, part.fft_row_stages);
    note(total.source_render_calls, part.source_render_calls);
    note(total.source_render_rows, part.source_render_rows);
    note(total.projected_columns, part.projected_columns);
    note(total.fir_input_rows, part.fir_input_rows);
    note(total.fir_output_contributions, part.fir_output_contributions);
    note(total.workspace_frequency_pairs, part.workspace_frequency_pairs);
    note(total.score_rows, part.score_rows);
    note(total.singleton_dot_terms, part.singleton_dot_terms);
    note(total.retained_peaks, part.retained_peaks);
    note(total.selection_peak_rows, part.selection_peak_rows);
}

void combine(CoarseOperationCounts& total, const CoarseBulkWork& part) {
    note(total.forward_ffts, part.forward_ffts);
    note(total.inverse_ffts, part.inverse_ffts);
    note(total.fft_rows, part.fft_rows);
    note(total.fft_row_stages, part.fft_row_stages);
    note(total.fir_input_rows, part.fir_input_rows);
    note(total.fir_output_contributions, part.fir_output_contributions);
    note(total.workspace_frequency_pairs, part.workspace_frequency_pairs);
    note(total.score_rows, part.score_rows);
    note(total.singleton_dot_terms, part.singleton_dot_terms);
}

void tracked_fft(std::span<Complex> values, bool inverse, const native_fft::DoublePlan& plan,
                 CoarseOperationCounts& work, std::stop_token stop) {
    native_fft::fft(values, inverse, plan, stop);
    note(inverse ? work.inverse_ffts : work.forward_ffts, 1);
    note(work.fft_rows, values.size());
    const auto levels = static_cast<std::uint64_t>(std::countr_zero(values.size()));
    require_coarse(!levels || values.size() <= UINT64_MAX / levels, "coarse transform work count overflow");
    note(work.fft_row_stages, levels * values.size());
}

void tracked_fft(std::span<std::complex<float>> values, bool inverse, const native_fft::FloatPlan& plan,
                 CoarseOperationCounts& work, std::stop_token stop) {
    native_fft::fft(values, inverse, plan, stop);
    note(inverse ? work.inverse_ffts : work.forward_ffts, 1);
    note(work.fft_rows, values.size());
    const auto levels = static_cast<std::uint64_t>(std::countr_zero(values.size()));
    require_coarse(!levels || values.size() <= UINT64_MAX / levels, "coarse transform work count overflow");
    note(work.fft_row_stages, levels * values.size());
}

void tracked_render(PatternSource& source, long double first, long double step,
                    std::span<Complex> output, CoarseOperationCounts& work) {
    std::array sources{&source};
    PatternSource::render_bank(sources, first, step, output.size(), output);
    note(work.source_render_calls, 1);
    note(work.source_render_rows, output.size());
}

template<class Job>
void parallel(std::size_t jobs, unsigned workers, std::stop_token outer_stop, ReceiverExecutor* executor,
              std::uint64_t public_slot, Job run) {
    if (executor) {
        executor->parallel(public_slot, jobs, workers, outer_stop, [&](unsigned, std::size_t index, std::stop_token stop) {
            run(index, stop);
        });
        return;
    }
    const auto active = std::min<std::size_t>(workers, jobs);
    if (!active) return;
    if (active == 1) {
        for (std::size_t index = 0; index < jobs; ++index) {
            check_stop(outer_stop);
            run(index, outer_stop);
        }
        return;
    }
    std::atomic_size_t next{0};
    std::stop_source cancellation;
    std::stop_callback propagate(outer_stop, [&] { cancellation.request_stop(); });
    std::vector<std::exception_ptr> errors(active);
    auto worker = [&](std::size_t id) {
        const auto stop = cancellation.get_token();
        try {
            while (!stop.stop_requested()) {
                const auto index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= jobs) return;
                run(index, stop);
            }
        } catch (...) {
            if (cancellation.request_stop()) errors[id] = std::current_exception();
        }
    };
    std::vector<execution::Task> tasks;
    tasks.reserve(active - 1);
    try {
        for (std::size_t id = 1; id < active; ++id) tasks.emplace_back([&, id] { worker(id); });
        worker(0);
    } catch (...) {
        cancellation.request_stop();
        throw;
    }
    for (auto& task : tasks) task.join();
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
    check_stop(outer_stop);
}

struct Layout {
    std::size_t source_rows = 0, raw_rows = 0, kernel_rows = 0, lag_count = 0;
    std::size_t crop_first = 0, crop_rows = 0, nfft = 0;
    long double origin = 0, source_start = 0;
};

struct Plan {
    Geometry source_geometry;
    Layout layout;
    Layout fidelity_layout;
    std::array<double, 3> clocks{};
    std::size_t fs = 0, output_fs = 0, decimation = 0, taps = 0, filter_fft = 0;
    std::size_t source_step = 1, clock_count = 0, ray_count = 0;
    std::size_t fidelity_source_first = 0, fidelity_capture_rows = 0;
    double source_rate = 0, pass_edge = 0, stop_edge = 0;
    double max_frequency_step = 0, separation = 0;

    std::size_t workspaces() const { return lanes * clock_count; }
    std::size_t workspace_index(std::size_t lane, std::size_t clock) const {
        return (lane / 2 * clock_count + clock) * 2 + lane % 2;
    }
};

std::size_t filtered_rows(std::size_t source_rows, const Plan& plan) {
    return add(source_rows, plan.taps - 2) / plan.decimation + 1;
}

Layout make_layout(const Plan& plan, const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                   std::size_t source_rows, std::size_t capture_rows, long double lead,
                   long double source_start) {
    const auto fs = static_cast<long double>(plan.fs);
    auto earliest = std::numeric_limits<long double>::infinity();
    auto latest = -earliest;
    for (std::size_t i = 0; i < plan.clock_count; ++i) {
        const double scale = 1 + plan.clocks[i] * 1e-6;
        earliest = std::min(earliest, (source_start - 2 / fs) / scale);
        latest = std::max(latest, (source_start + add(source_rows, 2) / fs) / scale);
    }
    Layout result;
    result.source_rows = source_rows;
    result.source_start = source_start;
    result.crop_first = extent(std::floor((earliest + lead + slot.arrival_lower_seconds) * plan.output_fs));
    result.origin = static_cast<long double>(result.crop_first) / plan.output_fs - lead - slot.arrival_lower_seconds;
    result.raw_rows = add(extent(std::ceil((latest + config.spread_delay_seconds - result.origin) * fs)), 1);
    result.kernel_rows = filtered_rows(result.raw_rows, plan);
    result.lag_count = add(extent(std::floor((slot.arrival_upper_seconds - slot.arrival_lower_seconds) *
                                            plan.output_fs + 1e-9)), 1);
    result.crop_rows = add(result.kernel_rows, result.lag_count - 1);
    result.nfft = fft_size(result.crop_rows);
    const auto required_raw = add(mul(add(result.crop_first, result.lag_count - 1), plan.decimation),
                                  result.raw_rows);
    require_coarse(required_raw <= capture_rows && add(result.crop_first, result.crop_rows) <=
                   filtered_rows(capture_rows, plan),
                   "coarse complete source support is outside the public observation window");
    return result;
}

std::size_t capture_samples(const ReceiverSlotPlan& slot) {
    require_coarse(slot.first_sample < slot.stop_sample && slot.stop_sample - slot.first_sample <= SIZE_MAX,
                   "coarse receiver requires a complete public window");
    return static_cast<std::size_t>(slot.stop_sample - slot.first_sample);
}

std::size_t smallest_projected_rate(std::size_t fs, double required) {
    auto selected = fs;
    for (std::size_t divisor = 1; divisor <= fs / divisor; ++divisor) {
        if (fs % divisor) continue;
        for (const auto rate : {divisor, fs / divisor})
            if (.4 * rate > required) selected = std::min(selected, rate);
    }
    return selected;
}

Plan make_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const auto capture_rows = capture_samples(slot);
    require_coarse(std::isfinite(config.sample_rate_hz) && config.sample_rate_hz >= 1 &&
                   config.sample_rate_hz <= 120'000'000 &&
                   std::floor(config.sample_rate_hz) == config.sample_rate_hz &&
                   std::isfinite(config.clock_bound_ppm) && config.clock_bound_ppm >= 0 &&
                   config.clock_bound_ppm < 1e6 && std::isfinite(config.spread_delay_seconds) &&
                   config.spread_delay_seconds >= 0 && std::isfinite(config.spread_doppler_hz) &&
                   std::isfinite(config.frequency_bound_hz) && config.frequency_bound_hz >= 0 &&
                   std::isfinite(config.noise_variance) && config.noise_variance > 0 &&
                   std::isfinite(slot.arrival_lower_seconds) && std::isfinite(slot.arrival_upper_seconds) &&
                   slot.arrival_lower_seconds <= slot.arrival_upper_seconds &&
                   std::isfinite(slot.window_capture_lead_seconds) && std::isfinite(slot.source_start_seconds) &&
                   slot.source_start_seconds >= 0 && slot.format.epoch >= 6 && slot.format.epoch <= 4'294'967'289,
                   "invalid public coarse receiver geometry");
    Plan plan;
    plan.fs = static_cast<std::size_t>(config.sample_rate_hz);
    plan.source_geometry = geometry(slot.format);
    plan.source_rate = effective_sample_rate(slot.format);
    const auto source_rows = extent(std::ceil(static_cast<long double>(plan.source_geometry.source_samples) *
                                              plan.fs / plan.source_rate));
    require_coarse(source_rows >= 4, "coarse receiver needs at least four finite source samples");
    if (config.clock_bound_ppm != 0) {
        plan.clocks = {-config.clock_bound_ppm, 0, config.clock_bound_ppm};
        plan.clock_count = 3;
    } else {
        plan.clock_count = 1;
    }
    plan.ray_count = config.spread_delay_seconds || config.spread_doppler_hz ? 2 : 1;
    const double required = .5 * plan.source_geometry.occupied_bandwidth_hz +
                            std::abs(config.spread_doppler_hz) + config.frequency_bound_hz;
    require_coarse(required < .4 * plan.fs, "coarse DATA prior is outside the projected passband");
    // Find the smallest integer divisor with sufficient public passband.
    const auto exact_projected_rate = smallest_projected_rate(plan.fs, required);
    plan.output_fs = exact_projected_rate;
    plan.pass_edge = .4 * plan.output_fs;
    plan.stop_edge = .49 * plan.output_fs;
    const auto exact_work = mul(source_rows, plan.workspaces() * plan.ray_count);
    if (slot.format.inner_rate_hz <= 1 && !slot.format.fhss && required < 4) {
        plan.source_step = 60;
        if (required < .4) {
            plan.output_fs = 10;
            plan.pass_edge = .4;
        } else if (required < 2) {
            plan.output_fs = 5;
            plan.pass_edge = 2;
        } else {
            plan.output_fs = 20;
            plan.pass_edge = 4;
        }
    } else if (exact_work > max_integer_template_work && slot.format.fhss && required < 240) {
        plan.source_step = 3;
        plan.output_fs = 600;
        plan.pass_edge = 240;
    }
    if (plan.source_step > 1) plan.stop_edge = .49 * plan.output_fs;
    // Other integral source rates can use the exact bank if the canonical
    // mapped projection lattice does not divide the receive rate.
    if (plan.fs % plan.output_fs) {
        plan.source_step = 1;
        plan.output_fs = exact_projected_rate;
        plan.pass_edge = .4 * plan.output_fs;
        plan.stop_edge = .49 * plan.output_fs;
    }
    const auto work = mul(ceil_div(source_rows, plan.source_step), plan.workspaces() * plan.ray_count);
    require_coarse(work <= (plan.source_step == 1 ? max_integer_template_work : max_mapped_template_work),
                   "full public template preparation exceeds the bounded native-work limit");
    plan.decimation = plan.fs / plan.output_fs;
    const double width = (plan.stop_edge - plan.pass_edge) / (plan.fs / 2.);
    plan.taps = extent(std::ceil((96. - 7.95) / 2.285 / (pi * width) + 1));
    plan.taps += 1 - plan.taps % 2;
    require_coarse(plan.taps <= 1'000'001, "coarse frontend FIR exceeds the public tap limit");
    plan.filter_fft = fft_size(add(chunk_samples, plan.taps - 1));
    plan.layout = make_layout(plan, config, slot, source_rows, capture_rows,
                              slot.window_capture_lead_seconds, slot.source_start_seconds);
    plan.max_frequency_step = receiver_coarse_frequency_step(slot.format, config.sample_rate_hz);
    plan.separation = .5 / plan.source_geometry.fine_chip_rate_hz;
    if (plan.source_step > 1) {
        plan.fidelity_source_first = extent(std::floor(
            static_cast<long double>(plan.source_geometry.calendar_origin_sample) * plan.fs / plan.source_rate +
            .5L * std::min(plan.source_geometry.tile_seconds, plan.source_geometry.bit_seconds) * plan.fs));
        require_coarse(plan.fidelity_source_first < source_rows && source_rows - plan.fidelity_source_first >= 256,
                       "local mapped-profile validation has no bounded source window");
        const auto source_count = std::min(2 * plan.fs, source_rows - plan.fidelity_source_first);
        plan.fidelity_capture_rows = extent(std::ceil((8.L + 2 + static_cast<long double>(source_count) / plan.fs +
            std::max(0., slot.arrival_upper_seconds) + config.spread_delay_seconds + 1) * plan.fs /
            (1 - config.clock_bound_ppm * 1e-6)));
        plan.fidelity_layout = make_layout(plan, config, slot, source_count, plan.fidelity_capture_rows, 8, 2);
    }
    return plan;
}

double bessel_i0(double value) {
    // beta <= 10 here. The positive power series converges to double
    // precision without a platform special-function dependency.
    const auto factor = .25 * value * value;
    double sum = 1, term = 1;
    for (unsigned k = 1; k <= 64; ++k) {
        term *= factor / (k * k);
        sum += term;
        if (term <= std::numeric_limits<double>::epsilon() * sum) break;
    }
    return sum;
}

std::vector<double> fir_taps(const Plan& plan, std::stop_token stop) {
    // Windowed sinc with the same 96 dB Kaiser design and DC normalization as
    // the reference rate-aware public projection.
    const auto beta = .1102 * (96. - 8.7);
    const auto denominator = bessel_i0(beta);
    const auto cut = (plan.pass_edge + plan.stop_edge) / plan.fs;
    const auto midpoint = (plan.taps - 1) / 2;
    std::vector<double> taps(plan.taps);
    for (std::size_t i = 0; i <= midpoint; ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        const auto j = static_cast<double>(midpoint - i);
        const auto x = j / static_cast<double>(midpoint);
        const auto sinc = j ? std::sin(pi * cut * j) / (pi * j) : cut;
        const auto value = sinc * bessel_i0(beta * std::sqrt(1 - x * x)) / denominator;
        taps[i] = value;
        taps[plan.taps - 1 - i] = value;
    }
    const auto norm = std::accumulate(taps.begin(), taps.end(), 0.);
    for (auto& value : taps) value /= norm;
    return taps;
}

class Projection {
public:
    Projection(const Plan& plan, const native_fft::DoublePlan* fft, CoarseBulk* bulk,
               CoarseOperationCounts& work, std::stop_token stop)
        : plan_(plan), fft_(fft), taps_(fir_taps(plan, stop)), bulk_(bulk) {
        if (bulk_) {
            CoarseBulkWork operations;
            device_filter_ = bulk_->prepare_filter(taps_, plan_.filter_fft, operations, stop);
            require_coarse(static_cast<bool>(device_filter_), "coarse bulk FIR filter setup returned no filter");
            combine(work, operations);
        } else {
            spectrum_.resize(plan.filter_fft);
            std::copy(taps_.begin(), taps_.end(), spectrum_.begin());
            tracked_fft(spectrum_, false, *fft_, work, stop);
        }
    }

    double variance(double raw) const {
        return raw * std::inner_product(taps_.begin(), taps_.end(), taps_.begin(), 0.);
    }
    std::span<const double> taps() const { return taps_; }
    CoarseBulk* bulk() const { return bulk_; }

    template<class Fill>
    std::vector<Complex> apply(std::size_t total, std::size_t crop_first, std::size_t crop_rows,
                               Fill fill, CoarseOperationCounts& work, std::stop_token stop) const {
        if (bulk_) {
            CoarseBulkWork operations;
            auto out = bulk_->project_fir(*device_filter_, {total, chunk_samples, 0, plan_.decimation,
                crop_first, crop_rows}, fill, operations, stop);
            require_coarse(out.size() == crop_rows, "coarse bulk FIR returned the wrong observation crop");
            combine(work, operations);
            return out;
        }
        std::vector<Complex> out(crop_rows), scratch(plan_.filter_fft);
        // Overlap-add complete finite convolution tails onto phase zero.
        // Only rows outside the requested output crop can be discarded.
        const auto crop_stop = add(crop_first, crop_rows);
        for (std::size_t first = 0; first < total;) {
            check_stop(stop);
            const auto size = std::min(chunk_samples, total - first);
            const auto tail_stop = add(first, add(size, taps_.size() - 1));
            const auto output_first = std::max(crop_first, ceil_div(first, plan_.decimation));
            const auto output_stop = std::min(crop_stop, ceil_div(tail_stop, plan_.decimation));
            if (output_first < output_stop) {
                fill(first, std::span(scratch).first(size));
                std::fill(scratch.begin() + size, scratch.end(), Complex{});
                tracked_fft(scratch, false, *fft_, work, stop);
                for (std::size_t i = 0; i < scratch.size(); ++i) scratch[i] *= spectrum_[i];
                tracked_fft(scratch, true, *fft_, work, stop);
                for (auto row = output_first; row < output_stop; ++row)
                    out[row - crop_first] += scratch[row * plan_.decimation - first];
                note(work.fir_input_rows, size);
                note(work.fir_output_contributions, output_stop - output_first);
            }
            first += size;
        }
        return out;
    }

private:
    const Plan& plan_;
    const native_fft::DoublePlan* fft_;
    std::vector<double> taps_;
    std::vector<Complex> spectrum_;
    CoarseBulk* bulk_;
    std::shared_ptr<const CoarseBulkFilter> device_filter_;
};

std::array<double, 4> cubic_weights(double u) {
    return {-u * (u - 1) * (u - 2) / 6, (u + 1) * (u - 1) * (u - 2) / 2,
            -(u + 1) * u * (u - 2) / 2, (u + 1) * u * (u - 1) / 6};
}

Complex cubic(std::span<const Complex> source, long double index) {
    if (index <= -2 || index >= static_cast<long double>(source.size()) + 1) return {};
    const auto center = static_cast<std::int64_t>(std::floor(index));
    const auto weights = cubic_weights(static_cast<double>(index - center));
    Complex out{};
    for (int i = 0; i < 4; ++i) {
        const auto sample = center + i - 1;
        if (sample >= 0 && static_cast<std::uint64_t>(sample) < source.size())
            out += weights[i] * source[static_cast<std::size_t>(sample)];
    }
    return out;
}

Complex rotation(long double cycles) {
    return std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * std::remainder(cycles, 1.L)));
}

struct RayGeometry {
    long double first_position, scale, first_cycles, cycle_step;
};

std::size_t mapped_edge_radius(const Plan& plan) {
    return 2 * plan.source_step + 3;
}

std::size_t mapped_edge_samples(const Plan& plan, const Layout& layout) {
    // The supported clock scale is below two. Cover that scale across the
    // correction radius plus the four-point interpolation stencil.
    return std::min(layout.source_rows, 2 * mapped_edge_radius(plan) + 6);
}

RayGeometry ray_geometry(const Plan& plan, const Layout& layout, const ReceiverConfig& config,
                         double clock, std::size_t ray) {
    const auto delay = ray ? config.spread_delay_seconds : 0;
    const auto doppler = ray ? config.spread_doppler_hz : 0;
    const auto scale = 1 + clock * 1e-6;
    return {((layout.origin - delay) * scale - layout.source_start) * plan.fs,
            scale, layout.origin * doppler, static_cast<long double>(doppler) / plan.fs};
}

std::vector<Complex> exact_column(const Layout& layout, const Projection& projection,
                                  std::span<const Complex> raw, const RayGeometry& ray,
                                  CoarseOperationCounts& work, std::stop_token stop) {
    return projection.apply(layout.raw_rows, 0, layout.kernel_rows,
        [&](std::size_t first, std::span<Complex> values) {
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto index = first + i;
                values[i] = cubic(raw, ray.first_position + index * ray.scale) *
                            rotation(ray.first_cycles + index * ray.cycle_step);
            }
        }, work, stop);
}

std::vector<Complex> integer_source(const Plan& plan, const Layout& layout, PatternSource& source,
                                    std::size_t source_first, CoarseOperationCounts& work, std::stop_token stop) {
    std::vector<Complex> raw(layout.source_rows);
    const auto scale = static_cast<long double>(plan.source_rate) / plan.fs;
    for (std::size_t i = 0; i < raw.size();) {
        check_stop(stop);
        const auto count = std::min(chunk_samples, raw.size() - i);
        tracked_render(source, (source_first + i) * scale, scale, std::span(raw).subspan(i, count), work);
        i += count;
    }
    return raw;
}

class MappedProjection {
public:
    MappedProjection(const Plan& plan, const Projection& projection, const native_fft::DoublePlan* fft,
                     CoarseOperationCounts& work, std::stop_token stop)
        : plan_(plan), projection_(projection), fft_(fft) {
        const auto step = plan_.source_step;
        const auto taps = projection_.taps();
        // Composition of the finite four-knot cardinal interpolant with the
        // original raw FIR. Only phases visited by the decimation are needed.
        std::vector<double> collapsed(add(taps.size(), 4 * step));
        const std::array<Complex, 5> impulse{0., 0., 1., 0., 0.};
        for (std::size_t i = 0; i <= 4 * step; ++i) {
            check_stop(stop);
            const auto weight = cubic(impulse, static_cast<long double>(i) / step).real();
            for (std::size_t j = 0; j < taps.size(); ++j) collapsed[i + j] += weight * taps[j];
        }
        const auto period = phase_count(plan_);
        phases_.reserve(period);
        for (std::size_t i = 0; i < period; ++i) {
            const auto first_coordinate = add(mul(i, plan_.decimation), 2 * step);
            const auto phase = first_coordinate % step;
            const auto count = (collapsed.size() - 1 - phase) / step + 1;
            const auto fft_rows = fft_size(add(chunk_samples, count - 1));
            auto& filter = phases_.emplace_back(Phase{
                count, first_coordinate / step, mul(period, plan_.decimation) / step, {}, {}});
            if (auto* bulk = projection_.bulk()) {
                std::vector<double> phase_taps(count);
                for (std::size_t j = 0; j < count; ++j) phase_taps[j] = collapsed[phase + j * step];
                CoarseBulkWork operations;
                filter.device_filter = bulk->prepare_filter(phase_taps, fft_rows, operations, stop);
                require_coarse(static_cast<bool>(filter.device_filter), "coarse bulk mapped FIR setup returned no filter");
                combine(work, operations);
            } else {
                filter.spectrum.resize(fft_rows);
                for (std::size_t j = 0; j < count; ++j) filter.spectrum[j] = collapsed[phase + j * step];
                tracked_fft(filter.spectrum, false, *fft_, work, stop);
            }
        }
    }

    static std::size_t phase_count(const Plan& plan) {
        return plan.source_step / std::gcd(plan.source_step, plan.decimation);
    }

    std::vector<Complex> apply(std::span<const Complex> knots, const Layout& layout, const RayGeometry& ray,
                               std::span<const Complex> first_edge, std::span<const Complex> last_edge,
                               CoarseOperationCounts& work, std::stop_token stop) const {
        std::vector<Complex> out(layout.kernel_rows);
        const auto period = phases_.size();
        for (std::size_t phase = 0; phase < period && phase < out.size(); ++phase) {
            const auto& filter = phases_[phase];
            const auto phase_rows = ceil_div(out.size() - phase, period);
            if (auto* bulk = projection_.bulk()) {
                CoarseBulkWork operations;
                const auto projected = bulk->project_fir(*filter.device_filter,
                    {knots.size(), chunk_samples, filter.start, filter.stride, 0, phase_rows},
                    [&](std::size_t first, std::span<Complex> values) {
                        std::copy_n(knots.begin() + first, values.size(), values.begin());
                    }, operations, stop);
                require_coarse(projected.size() == phase_rows, "coarse bulk FIR returned the wrong mapped phase crop");
                for (std::size_t i = 0; i < projected.size(); ++i) out[phase + i * period] = projected[i];
                combine(work, operations);
                continue;
            }
            std::vector<Complex> scratch(filter.spectrum.size());
            for (std::size_t first = 0; first < knots.size();) {
                check_stop(stop);
                const auto count = std::min(chunk_samples, knots.size() - first);
                const auto tail_stop = add(first, add(count, filter.taps - 1));
                const auto output_first = first > filter.start ? ceil_div(first - filter.start, filter.stride) : 0;
                const auto output_stop = tail_stop > filter.start ?
                    std::min(phase_rows, ceil_div(tail_stop - filter.start, filter.stride)) : 0;
                if (output_first < output_stop) {
                    std::copy_n(knots.begin() + first, count, scratch.begin());
                    std::fill(scratch.begin() + count, scratch.end(), Complex{});
                    tracked_fft(scratch, false, *fft_, work, stop);
                    for (std::size_t i = 0; i < scratch.size(); ++i) scratch[i] *= filter.spectrum[i];
                    tracked_fft(scratch, true, *fft_, work, stop);
                    for (auto i = output_first; i < output_stop; ++i)
                        out[phase + i * period] += scratch[filter.start + i * filter.stride - first];
                    note(work.fir_input_rows, count);
                    note(work.fir_output_contributions, output_stop - output_first);
                }
                first += count;
            }
        }
        // The interpolation spills past the finite raw column at both edges.
        // Remove precisely the samples that were outside the original operator.
        const auto taps = projection_.taps();
        const auto step = static_cast<std::int64_t>(plan_.source_step);
        const auto decimation = static_cast<std::int64_t>(plan_.decimation);
        require_coarse(layout.raw_rows <= INT64_MAX - (3 * plan_.source_step + 2 + taps.size()),
                       "coarse mapped boundary coordinate overflow");
        const auto raw_end = static_cast<std::int64_t>(layout.raw_rows);
        auto subtract = [&](std::int64_t first, std::int64_t stop_at) {
            for (auto index = first; index < stop_at; ++index) {
                const auto value = cubic(knots, static_cast<long double>(index) / step);
                if (value == Complex{}) continue;
                const auto stop_raw = index + static_cast<std::int64_t>(taps.size());
                if (stop_raw <= 0) continue;
                const auto begin = index > 0 ? ceil_div(static_cast<std::size_t>(index), plan_.decimation) : 0;
                const auto end = std::min(out.size(), ceil_div(static_cast<std::size_t>(stop_raw), plan_.decimation));
                for (auto i = begin; i < end; ++i)
                    out[i] -= value * taps[static_cast<std::size_t>(static_cast<std::int64_t>(i) * decimation - index)];
                if (end > begin) note(work.fir_output_contributions, end - begin);
            }
        };
        subtract(-2 * step, 0);
        subtract(raw_end, raw_end + 3 * step + 2);

        // Cubic interpolation across a finite source edge smears the cutoff
        // between knots. Restore the original raw samples in a bounded
        // neighborhood and project only their residual through the raw FIR.
        // The same correction applies to the local fidelity slice and the
        // operational template, including clock and Doppler mapping.
        const auto radius = static_cast<long double>(mapped_edge_radius(plan_));
        const auto clip = [&](long double row) {
            return static_cast<std::size_t>(std::clamp(row, 0.L, static_cast<long double>(layout.raw_rows)));
        };
        const auto correct = [&](std::size_t source_edge, std::span<const Complex> samples,
                                 std::size_t source_first, std::size_t previous_stop) {
            const auto raw_edge = (source_edge - ray.first_position) / ray.scale;
            const auto first = std::max(previous_stop, clip(std::ceil(raw_edge - radius)));
            const auto stop_at = clip(std::floor(raw_edge + radius) + 1);
            check_stop(stop);
            for (auto index = first; index < stop_at; ++index) {
                const auto position = ray.first_position + index * ray.scale;
                const auto exact = cubic(samples, position - source_first) *
                                   rotation(ray.first_cycles + index * ray.cycle_step);
                const auto residual = exact - cubic(knots, static_cast<long double>(index) / step);
                const auto begin = ceil_div(index, plan_.decimation);
                const auto end = std::min(out.size(), ceil_div(add(index, taps.size()), plan_.decimation));
                for (auto row = begin; row < end; ++row)
                    out[row] += residual * taps[row * plan_.decimation - index];
                if (end > begin) {
                    note(work.fir_input_rows, 1);
                    note(work.fir_output_contributions, end - begin);
                }
            }
            return std::max(previous_stop, stop_at);
        };
        const auto first_stop = correct(0, first_edge, 0, 0);
        correct(layout.source_rows, last_edge, layout.source_rows - last_edge.size(), first_stop);
        return out;
    }

private:
    struct Phase {
        std::size_t taps, start, stride;
        std::vector<Complex> spectrum;
        std::shared_ptr<const CoarseBulkFilter> device_filter;
    };
    const Plan& plan_;
    const Projection& projection_;
    const native_fft::DoublePlan* fft_;
    std::vector<Phase> phases_;
};

std::vector<Complex> mapped_column(const Plan& plan, const Layout& layout, PatternSource& source,
                                   std::size_t source_first, const MappedProjection& projection,
                                   const RayGeometry& ray, CoarseOperationCounts& work,
                                   std::stop_token stop) {
    const auto step = plan.source_step;
    std::vector<Complex> knots(add(ceil_div(layout.raw_rows - 1, step), 1));
    const auto rate_ratio = static_cast<long double>(plan.source_rate) / plan.fs;
    const auto position = [&](std::size_t index) { return ray.first_position + index * step * ray.scale; };
    const auto last_interior = static_cast<long double>(layout.source_rows - 2);
    const auto edge_samples = mapped_edge_samples(plan, layout);
    std::vector<Complex> first_edge(edge_samples), last_edge(edge_samples);
    check_stop(stop);
    tracked_render(source, source_first * rate_ratio, rate_ratio, first_edge, work);
    tracked_render(source, add(source_first, layout.source_rows - last_edge.size()) * rate_ratio,
                   rate_ratio, last_edge, work);
    for (std::size_t first = 0; first < knots.size();) {
        check_stop(stop);
        const auto stop_at = add(first, std::min(chunk_samples, knots.size() - first));
        auto middle = first;
        while (middle < stop_at && position(middle) < 1) {
            knots[middle] = cubic(first_edge, position(middle));
            ++middle;
        }
        auto end = stop_at;
        while (end > middle && position(end - 1) > last_interior) {
            --end;
            knots[end] = cubic(last_edge, position(end) - (layout.source_rows - last_edge.size()));
        }
        if (middle < end) {
            tracked_render(source, (source_first + position(middle)) * rate_ratio,
                           step * ray.scale * rate_ratio,
                           std::span(knots).subspan(middle, end - middle), work);
        }
        for (auto i = first; i < stop_at; ++i)
            if (knots[i] != Complex{}) knots[i] *= rotation(ray.first_cycles + i * step * ray.cycle_step);
        first = stop_at;
    }
    // The terminal interpolation knot beyond the raw endpoint is defined zero.
    if ((knots.size() - 1) * step >= layout.raw_rows) knots.back() = {};
    return projection.apply(knots, layout, ray, first_edge, last_edge, work, stop);
}

using Columns = std::array<std::vector<Complex>, 2>;

struct Hermitian {
    double a = 0, d = 0;
    Complex b{};
};

double hermitian_norm(const Hermitian& x) {
    return (std::abs(x.a + x.d) + std::hypot(x.a - x.d, 2 * std::abs(x.b))) / 2;
}

double frobenius_squared(const Hermitian& x) { return x.a * x.a + x.d * x.d + 2 * std::norm(x.b); }

Complex dot(std::span<const Complex> first, std::span<const Complex> second) {
    Complex out{};
    for (std::size_t i = 0; i < first.size(); ++i) out += std::conj(first[i]) * second[i];
    return out;
}

Hermitian gram(const Columns& columns, std::size_t rays) {
    Hermitian result;
    result.a = dot(columns[0], columns[0]).real();
    if (rays == 2) {
        result.d = dot(columns[1], columns[1]).real();
        result.b = dot(columns[0], columns[1]);
    }
    return result;
}

struct Inverse {
    Hermitian matrix;
    double largest = 0;
    unsigned rank = 0;
};

Inverse invert(const Hermitian& g, std::size_t rays) {
    const auto spread = std::hypot(g.a - g.d, 2 * std::abs(g.b));
    const auto largest = (g.a + g.d + spread) / 2;
    require_coarse(std::isfinite(largest) && largest > 0, "coarse pattern has no observable energy");
    if (rays == 1) return {{1 / g.a, 0, {}}, largest, 1};
    const auto det = g.a * g.d - std::norm(g.b);
    const auto smallest = std::max(0., det / largest);
    if (smallest > largest * rank_cutoff)
        return {{g.d / det, g.a / det, -g.b / det}, largest, 2};
    // Rank-one spectral pseudoinverse, using the retained eigenprojector.
    const auto scale = largest * (largest - smallest);
    return {{(g.a - smallest) / scale, (g.d - smallest) / scale, g.b / scale}, largest, 1};
}

double quadratic(const Hermitian& inv, Complex a, Complex b) {
    auto result = inv.a * std::norm(a);
    result += inv.d * std::norm(b);
    result += 2 * std::real(std::conj(a) * inv.b * b);
    return result;
}

Columns make_columns(const Plan& plan, const Layout& layout, const ReceiverConfig& config,
                     const Projection& projection, const MappedProjection* mapped,
                     PatternSource& source, std::size_t source_first, std::span<const Complex> integer,
                     double clock, CoarseOperationCounts& work, std::stop_token stop) {
    Columns out;
    for (std::size_t ray = 0; ray < plan.ray_count; ++ray) {
        const auto g = ray_geometry(plan, layout, config, clock, ray);
        out[ray] = mapped ? mapped_column(plan, layout, source, source_first, *mapped, g, work, stop) :
                            exact_column(layout, projection, integer, g, work, stop);
        note(work.projected_columns, 1);
    }
    return out;
}

struct Workspace {
    Columns columns;
    Inverse inverse;
    std::array<std::vector<std::complex<float>>, 2> float_columns;
    double eigenvalue_ratio = 1;
    int scale_exponent = 0;
    native_fft::Q15Spectrum packed_column;
};

// Block scaling changes only representation, after native channel isolation.
// Ordinary normalized audio needs no extra multiplication. Rare exponent
// extremes are scaled by an exact power of two and restored in FP64 scoring.
int float_scale_exponent(double maximum) {
    require_coarse(std::isfinite(maximum) && maximum >= 0, "nonfinite post-filter FFT scale");
    if (!maximum) return 0;
    const auto exponent = std::ilogb(maximum);
    return exponent < -16 || exponent > 16 ? exponent : 0;
}

Complex scaled(Complex value, int exponent) {
    if (!exponent) return value;
    return {std::scalbn(value.real(), exponent), std::scalbn(value.imag(), exponent)};
}

double maximum_component(std::span<const Complex> values) {
    double maximum = 0;
    for (const auto value : values)
        maximum = std::max({maximum, std::abs(value.real()), std::abs(value.imag())});
    return maximum;
}

void to_float_spectrum(Workspace& workspace, const Layout& layout, const native_fft::FloatPlan& fft,
                       CoarseOperationCounts& work, std::stop_token stop, bool pack = false) {
    const auto maximum = std::max(maximum_component(workspace.columns[0]), maximum_component(workspace.columns[1]));
    workspace.scale_exponent = float_scale_exponent(maximum);
    for (std::size_t ray = 0; ray < workspace.columns.size(); ++ray) if (!workspace.columns[ray].empty()) {
        auto& input = workspace.columns[ray];
        auto& spectrum = workspace.float_columns[ray];
        spectrum.resize(layout.nfft);
        for (std::size_t i = 0; i < input.size(); ++i) {
            if ((i & 4095U) == 0) check_stop(stop);
            spectrum[i] = static_cast<std::complex<float>>(scaled(input[i], -workspace.scale_exponent));
        }
        // No complete double and float spectral banks coexist. The existing
        // native quote covers each raw column and its transient destination.
        std::vector<Complex>().swap(input);
        tracked_fft(spectrum, false, fft, work, stop);
        for (auto& value : spectrum) value = std::conj(value);
        if (pack) {
            workspace.packed_column = native_fft::pack_q15(spectrum, stop);
            std::vector<std::complex<float>>().swap(spectrum);
        }
    }
}

void to_spectrum(Columns& columns, const Layout& layout, const native_fft::DoublePlan& fft,
                 CoarseOperationCounts& work, std::stop_token stop) {
    for (auto& ray : columns) if (!ray.empty()) {
        std::vector<Complex> spectrum(layout.nfft);
        std::copy(ray.begin(), ray.end(), spectrum.begin());
        tracked_fft(spectrum, false, fft, work, stop);
        for (auto& value : spectrum) value = std::conj(value);
        ray = std::move(spectrum);
    }
}

std::vector<Workspace> prepare_bank(const Plan& plan, const ReceiverConfig& config,
                                   const ReceiverSlotPlan& slot, const Crypto& key,
                                   const Projection& projection, const MappedProjection* mapped,
                                   const native_fft::DoublePlan* spectrum_fft,
                                   const native_fft::FloatPlan* float_spectrum_fft, bool packed,
                                   unsigned workers, CoarseOperationCounts& work, std::stop_token stop,
                                   ReceiverExecutor* executor) {
    std::vector<Workspace> bank(plan.workspaces());
    std::array<CoarseOperationCounts, lanes> lane_work{};
    parallel(lanes, workers, stop, executor, slot.index, [&](std::size_t lane, std::stop_token job_stop) {
        auto format = slot.format;
        format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(format.epoch) + receiver_epoch_offsets[lane / 2]);
        PatternSource source(format, key, static_cast<std::uint8_t>(lane % 2));
        auto& counts = lane_work[lane];
        const auto integer = mapped ? std::vector<Complex>{} : integer_source(plan, plan.layout, source, 0, counts, job_stop);
        for (std::size_t clock = 0; clock < plan.clock_count; ++clock) {
            check_stop(job_stop);
            auto columns = make_columns(plan, plan.layout, config, projection, mapped, source, 0, integer,
                                        plan.clocks[clock], counts, job_stop);
            auto& destination = bank[plan.workspace_index(lane, clock)];
            const auto g = gram(columns, plan.ray_count);
            destination.inverse = invert(g, plan.ray_count);
            if (plan.ray_count == 2) {
                const auto smallest = std::max(0., (g.a * g.d - std::norm(g.b)) / destination.inverse.largest);
                destination.eigenvalue_ratio = smallest / destination.inverse.largest;
            }
            if (spectrum_fft) to_spectrum(columns, plan.layout, *spectrum_fft, counts, job_stop);
            destination.columns = std::move(columns);
            if (float_spectrum_fft)
                to_float_spectrum(destination, plan.layout, *float_spectrum_fft, counts, job_stop, packed);
        }
    });
    for (const auto& counts : lane_work) combine(work, counts);
    return bank;
}

struct Peak {
    double score = -infinity;
    std::size_t frequency = 0, lag = 0;
};

std::size_t arrival_peak_slots(std::size_t arrivals) {
    // A single arrival can supply only one peak at a frequency. The final
    // workspace selection can still retain two different frequencies.
    return std::min(arrivals, retained_per_bit);
}

std::array<Peak, retained_per_bit> arrival_peaks(std::span<const double> values,
                                               std::size_t distance, std::size_t frequency) {
    const auto selected = select_coarse_arrivals(values, distance);
    std::array<Peak, retained_per_bit> result{};
    for (std::size_t i = 0; i < selected.count; ++i) {
        const auto lag = selected.indices[i];
        result[i] = {values[lag], frequency, lag};
    }
    return result;
}

template<class Fft>
using FftComplex = std::conditional_t<std::is_same_v<Fft, native_fft::FloatPlan>, std::complex<float>, Complex>;

template<class Fft>
std::vector<double> correlation_scores(const Plan& plan, const Layout& layout, const Workspace& workspace,
                                       std::span<const FftComplex<Fft>> observation, double variance,
                                       const Fft& fft, CoarseOperationCounts& work,
                                       std::stop_token stop, int observation_exponent = 0,
                                       const native_fft::Q15Spectrum* packed = nullptr) {
    std::array<std::vector<FftComplex<Fft>>, 2> products;
    const auto& columns = [&]() -> const auto& {
        if constexpr (std::is_same_v<Fft, native_fft::FloatPlan>) return workspace.float_columns;
        else return workspace.columns;
    }();
    for (std::size_t ray = 0; ray < plan.ray_count; ++ray) {
        auto& product = products[ray];
        product.resize(layout.nfft);
        if constexpr (std::is_same_v<Fft, native_fft::FloatPlan>) {
            if (packed) native_fft::multiply_spectra(product, *packed, workspace.packed_column, stop);
            else native_fft::multiply_spectra(product, observation, columns[ray], stop);
        } else {
            for (std::size_t i = 0; i < product.size(); ++i)
                product[i] = observation[i] * columns[ray][i];
        }
        tracked_fft(product, true, fft, work, stop);
    }
    std::vector<double> scores(layout.lag_count);
    for (std::size_t lag = 0; lag < scores.size(); ++lag) {
        const auto exponent = observation_exponent + workspace.scale_exponent +
            (packed ? packed->exponent() + workspace.packed_column.exponent() : 0);
        const auto first = scaled(static_cast<Complex>(products[0][lag]), exponent);
        const auto second = plan.ray_count == 2 ? scaled(static_cast<Complex>(products[1][lag]), exponent) : Complex{};
        scores[lag] = quadratic(workspace.inverse.matrix, first, second) / variance;
        require_coarse(std::isfinite(scores[lag]), "nonfinite coarse correlation score");
    }
    note(work.workspace_frequency_pairs, 1);
    note(work.score_rows, scores.size());
    return scores;
}

template<class Fft>
std::vector<FftComplex<Fft>> observation_spectrum(const Layout& layout, std::span<const Complex> observation,
                                          double frequency, std::size_t output_fs,
                                          const Fft& fft, CoarseOperationCounts& work,
                                          std::stop_token stop, int scale_exponent = 0) {
    std::vector<FftComplex<Fft>> spectrum(layout.nfft);
    if (!frequency) {
        for (std::size_t i = 0; i < observation.size(); ++i)
            spectrum[i] = static_cast<FftComplex<Fft>>(scaled(observation[i], -scale_exponent));
    } else {
        const auto step = static_cast<long double>(frequency) / output_fs;
        const auto increment = rotation(-step);
        Complex phase{1};
        for (std::size_t i = 0; i < observation.size(); ++i) {
            if ((i & 255U) == 0) phase = rotation(-static_cast<long double>(i) * step);
            spectrum[i] = static_cast<FftComplex<Fft>>(scaled(observation[i], -scale_exponent) * phase);
            phase *= increment;
        }
    }
    tracked_fft(spectrum, false, fft, work, stop);
    return spectrum;
}

struct Orthonormal {
    Columns q;
    unsigned rank = 0;
};

Orthonormal orthonormal(const Columns& columns, const Hermitian& g, const Inverse& inv, std::size_t rays) {
    Orthonormal result;
    result.rank = inv.rank;
    auto& q = result.q;
    q[0].resize(columns[0].size());
    if (rays == 1 || inv.rank == 2) {
        const auto dominant = g.a >= g.d ? 0 : 1;
        const auto gain = 1 / std::sqrt(dominant ? g.d : g.a);
        for (std::size_t i = 0; i < q[0].size(); ++i) q[0][i] = gain * columns[dominant][i];
        if (inv.rank == 2) {
            q[1] = columns[1 - dominant];
            // Reorthogonalization keeps the projector distance meaningful
            // next to the same 1e-10 relative numerical rank cutoff.
            for (unsigned pass = 0; pass < 2; ++pass) {
                const auto component = dot(q[0], q[1]);
                for (std::size_t i = 0; i < q[1].size(); ++i) q[1][i] -= component * q[0][i];
            }
            const auto norm = std::sqrt(dot(q[1], q[1]).real());
            require_coarse(std::isfinite(norm) && norm > 0, "degenerate coarse fidelity projector");
            for (auto& value : q[1]) value /= norm;
        }
    } else {
        const Complex v0 = g.a >= g.d ? Complex(inv.largest - g.d) : g.b;
        const Complex v1 = g.a >= g.d ? std::conj(g.b) : Complex(inv.largest - g.a);
        for (std::size_t i = 0; i < q[0].size(); ++i) q[0][i] = v0 * columns[0][i] + v1 * columns[1][i];
        const auto norm = std::sqrt(dot(q[0], q[0]).real());
        require_coarse(std::isfinite(norm) && norm > 0, "degenerate coarse fidelity projector");
        for (auto& value : q[0]) value /= norm;
    }
    return result;
}

double projector_distance(const Orthonormal& a, const Orthonormal& b) {
    if (a.rank != b.rank) return 1.;
    const auto m00 = dot(a.q[0], b.q[0]);
    double minimum_squared = std::norm(m00);
    if (a.rank == 2) {
        const auto m01 = dot(a.q[0], b.q[1]);
        const auto m10 = dot(a.q[1], b.q[0]);
        const auto m11 = dot(a.q[1], b.q[1]);
        const auto d0 = std::norm(m00) + std::norm(m10);
        const auto d1 = std::norm(m01) + std::norm(m11);
        const auto off = std::conj(m00) * m01 + std::conj(m10) * m11;
        minimum_squared = (d0 + d1 - std::hypot(d0 - d1, 2 * std::abs(off))) / 2;
    }
    return std::sqrt(1 - std::clamp(minimum_squared, 0., 1.));
}

double scoring_operator_error(const Orthonormal& q, const Columns& columns, const Inverse& inv,
                              std::size_t rays) {
    std::array<std::array<Complex, 2>, 2> c{};
    for (std::size_t i = 0; i < q.rank; ++i)
        for (std::size_t j = 0; j < rays; ++j) c[i][j] = dot(q.q[i], columns[j]);
    const auto& g = inv.matrix;
    auto element = [&](std::size_t row, std::size_t col) {
        return (c[row][0] * g.a + c[row][1] * std::conj(g.b)) * std::conj(c[col][0]) +
               (c[row][0] * g.b + c[row][1] * g.d) * std::conj(c[col][1]);
    };
    // Rank-deficient scoring removes the orthogonal discarded eigenvector.
    // Bound roundoff leakage out of q as well as the error inside its span.
    if (q.rank == 1 && rays == 2) {
        const Complex v0 = g.a >= g.d ? Complex(inv.largest * g.a) : inv.largest * g.b;
        const Complex v1 = g.a >= g.d ? inv.largest * std::conj(g.b) : Complex(inv.largest * g.d);
        const auto n = std::hypot(std::abs(v0), std::abs(v1));
        const auto projection = (v0 * c[0][0] + v1 * c[0][1]) / n;
        const auto missing = std::max(0., inv.largest - std::norm(projection)) / inv.largest;
        return std::abs(element(0, 0).real() - 1) + 2 * std::sqrt(missing) + missing;
    }
    if (q.rank == 1) return std::abs(element(0, 0).real() - 1);
    return hermitian_norm({element(0, 0).real() - 1, element(1, 1).real() - 1, element(0, 1)});
}

std::array<std::vector<Complex>, 2> fidelity_controls(const Plan& plan, const ReceiverConfig& config,
        const ReceiverSlotPlan& slot, const Crypto& key, const Projection& projection,
        const native_fft::DoublePlan& fft, CoarseOperationCounts& work, std::stop_token stop) {
    const auto& layout = plan.fidelity_layout;
    PatternSource source(slot.format, key, 0);
    const auto source_slice = integer_source(plan, layout, source, plan.fidelity_source_first, work, stop);
    const auto best_zero_lag = std::min(layout.lag_count - 1, extent(std::max(0.,
        std::round(-slot.arrival_lower_seconds * plan.output_fs))));
    const auto arrival = slot.arrival_lower_seconds + static_cast<double>(best_zero_lag) / plan.output_fs;
    std::array<std::vector<Complex>, 2> controls;
    std::mt19937 random(21733);
    std::normal_distribution<double> noise(0., 1 / std::sqrt(2.));
    for (std::size_t mode = 0; mode < controls.size(); ++mode) {
        const auto projected = projection.apply(plan.fidelity_capture_rows, layout.crop_first, layout.crop_rows,
            [&](std::size_t first, std::span<Complex> values) {
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const auto t = static_cast<long double>(first + i) / plan.fs - 8;
                    auto value = Complex(.8, .2) * cubic(source_slice, (t - arrival - 2) * plan.fs);
                    value += Complex(.3, -.6) * cubic(source_slice, (t - arrival - config.spread_delay_seconds - 2) * plan.fs) *
                             rotation(config.spread_doppler_hz * t);
                    if (mode) {
                        const auto real = noise(random);
                        value += Complex(real, noise(random));
                    }
                    values[i] = value;
                }
            }, work, stop);
        controls[mode] = observation_spectrum(layout, projected, 0, plan.output_fs, fft, work, stop);
    }
    return controls;
}

void validate_mapped_profile(const Plan& plan, const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                             const Crypto& key, const Projection& projection, const MappedProjection& mapped,
                             const native_fft::DoublePlan& fft, unsigned workers, CoarseOperationCounts& work,
                             std::stop_token stop, ReceiverExecutor* executor) {
    const auto& layout = plan.fidelity_layout;
    const auto observations = fidelity_controls(plan, config, slot, key, projection, fft, work, stop);
    using Scores = std::array<std::array<double, 2>, 2>; // exact/mapped, clean/noisy
    std::vector<Scores> scores(plan.workspaces());
    std::array<CoarseOperationCounts, receiver_epoch_offsets.size()> epoch_work{};
    parallel(receiver_epoch_offsets.size(), workers, stop, executor, slot.index, [&](std::size_t epoch, std::stop_token job_stop) {
        auto format = slot.format;
        format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(format.epoch) + receiver_epoch_offsets[epoch]);
        std::array<PatternSource, 2> sources{PatternSource(format, key, 0), PatternSource(format, key, 1)};
        std::array<std::vector<Complex>, 2> integer;
        auto& counts = epoch_work[epoch];
        for (std::size_t bit = 0; bit < 2; ++bit)
            integer[bit] = integer_source(plan, layout, sources[bit], plan.fidelity_source_first, counts, job_stop);
        for (std::size_t clock = 0; clock < plan.clock_count; ++clock) {
            std::array<Orthonormal, 2> reference_basis, mapped_basis;
            for (std::size_t bit = 0; bit < 2; ++bit) {
                auto exact = make_columns(plan, layout, config, projection, nullptr, sources[bit],
                    plan.fidelity_source_first, integer[bit], plan.clocks[clock], counts, job_stop);
                auto approximate = make_columns(plan, layout, config, projection, &mapped, sources[bit],
                    plan.fidelity_source_first, {}, plan.clocks[clock], counts, job_stop);
                const auto exact_g = gram(exact, plan.ray_count), mapped_g = gram(approximate, plan.ray_count);
                const auto exact_inverse = invert(exact_g, plan.ray_count), mapped_inverse = invert(mapped_g, plan.ray_count);
                require_coarse(exact_inverse.rank == mapped_inverse.rank,
                               "local mapped-template fidelity rank mismatch");
                auto& before = reference_basis[bit] = orthonormal(exact, exact_g, exact_inverse, plan.ray_count);
                auto& after = mapped_basis[bit] = orthonormal(approximate, mapped_g, mapped_inverse, plan.ray_count);
                const auto distance = projector_distance(before, after);
                const auto scoring_bound = distance + scoring_operator_error(before, exact, exact_inverse, plan.ray_count) +
                    scoring_operator_error(after, approximate, mapped_inverse, plan.ray_count);
                double difference = 0;
                for (std::size_t ray = 0; ray < plan.ray_count; ++ray)
                    for (std::size_t i = 0; i < layout.kernel_rows; ++i)
                        difference += std::norm(exact[ray][i] - approximate[ray][i]);
                const Hermitian gram_delta{exact_g.a - mapped_g.a, exact_g.d - mapped_g.d, exact_g.b - mapped_g.b};
                require_coarse(distance <= .01 && scoring_bound <= .01 &&
                    difference <= .01 * .01 * (exact_g.a + exact_g.d) &&
                    frobenius_squared(gram_delta) <= .02 * .02 * frobenius_squared(exact_g),
                    "local mapped-template projector fidelity gate failed");
                to_spectrum(exact, layout, fft, counts, job_stop);
                to_spectrum(approximate, layout, fft, counts, job_stop);
                std::array<Workspace, 2> pair{{{std::move(exact), exact_inverse}, {std::move(approximate), mapped_inverse}}};
                auto& result = scores[plan.workspace_index(2 * epoch + bit, clock)];
                for (std::size_t method = 0; method < pair.size(); ++method)
                    for (std::size_t input = 0; input < observations.size(); ++input) {
                        const auto row = correlation_scores(plan, layout, pair[method], observations[input], 1, fft, counts, job_stop);
                        result[method][input] = *std::max_element(row.begin(), row.end());
                    }
            }
            require_coarse(projector_distance(mapped_basis[0], mapped_basis[1]) >=
                .9 * std::max(1e-30, projector_distance(reference_basis[0], reference_basis[1])),
                "local mapped-template wrong-bit separation gate failed");
        }
    });
    for (const auto& counts : epoch_work) combine(work, counts);
    for (std::size_t input = 0; input < observations.size(); ++input) {
        std::array<std::size_t, 2> winners{};
        for (std::size_t method = 0; method < winners.size(); ++method) {
            auto best = -infinity;
            // The across-bit tie order places bit zero before bit one.
            for (std::size_t bit = 0; bit < 2; ++bit)
                for (std::size_t i = bit; i < scores.size(); i += 2)
                    if (scores[i][method][input] > best) {
                        best = scores[i][method][input];
                        winners[method] = (i / (2 * plan.clock_count)) * 2 + bit;
                    }
        }
        require_coarse(winners[0] == winners[1], "local mapped-template clean/noisy winner gate failed");
    }
}

std::size_t frequency_bound(const Plan& plan, const ReceiverConfig& config) {
    const auto full_count = std::ceil(2 * config.frequency_bound_hz / plan.max_frequency_step) + 1;
    require_coarse(full_count <= max_frequency_points, "full fallback coarse grid exceeds its public point limit");
    const auto count = full_count + (config.helper_enabled ? 3 * (max_intervals - 1) : 0);
    return count >= max_frequency_points ? max_frequency_points : extent(count);
}

struct FrequencyGroup {
    std::size_t first, count;
    double lower, upper;
};

struct FrequencyGrid {
    std::vector<double> values;
    std::vector<FrequencyGroup> groups;
};

FrequencyGrid frequencies(const Plan& plan, const ReceiverConfig& config,
                          std::span<const FrequencyInterval> intervals) {
    require_coarse(!intervals.empty() && intervals.size() <= (config.helper_enabled ? max_intervals : 1),
                   "coarse frequency interval union is outside the public limit");
    FrequencyGrid grid;
    auto& values = grid.values;
    const auto reserved = frequency_bound(plan, config);
    values.reserve(reserved);
    grid.groups.reserve(intervals.size());
    auto prior_upper = -infinity;
    for (auto [lower, upper] : intervals) {
        require_coarse(std::isfinite(lower) && std::isfinite(upper) && lower <= upper && lower >= prior_upper &&
                       lower >= -config.frequency_bound_hz && upper <= config.frequency_bound_hz,
                       "coarse frequency intervals must be ordered, disjoint and inside the public prior");
        prior_upper = upper;
        const auto steps = std::ceil((upper - lower) / plan.max_frequency_step);
        const bool shared_endpoint = !values.empty() && values.back() == lower;
        require_coarse(std::isfinite(steps) && steps < max_frequency_points &&
                       values.size() + steps + 1 - shared_endpoint <= reserved,
                       "complete coarse frequency search exceeds its public point limit");
        const auto count = extent(steps);
        const auto step = count ? (upper - lower) / count : 0.;
        grid.groups.push_back({values.size() - shared_endpoint, count + 1, lower, upper});
        for (std::size_t i = shared_endpoint; i <= count; ++i)
            values.push_back(i == count ? upper : lower + i * step);
    }
    return grid;
}

std::size_t tile_points(const Plan& plan, std::size_t frequencies, unsigned workers) {
    // Every tile evaluates all alternatives and fits the 32768-product
    // kernel bound. Larger searches retain all peaks across every tile.
    const auto products_per_frequency = plan.workspaces() * plan.ray_count;
    return std::min({frequencies, max_fft_products / products_per_frequency,
                     std::max<std::size_t>(4, std::min<std::size_t>(workers, 32))});
}

bool native_float_kernel() {
    return std::string_view(native_fft::kernel_name()) == "avx2";
}

// The matched Radeon/CPU complete receiver case had 26 workspaces, 1750
// projected rows and 1794 frequencies. The native 4096-point chirp-Z
// search beat packed GPU direct scoring with 24 admitted workers.
constexpr double matched_chirp_stages = (2 * 26 + 1) * 4096 * 12.;
constexpr double matched_direct_terms = 26. * 1750 * 1794;
constexpr double matched_chirp_workers = 24.;

bool exact_chirp_z_profile(const Plan& plan, const ReceiverConfig& config) {
    if (plan.source_step != 1 || plan.ray_count != 1 || plan.layout.lag_count != 1 ||
        config.helper_enabled || config.native_coarse_precision != NativeCoarsePrecision::automatic)
        return false;
    if (!config.bulk_backend) return true;
    const auto rate = static_cast<double>(plan.output_fs), clock = 2 * config.clock_bound_ppm;
    if (config.bulk_backend->supports_exact_device_chirp_z())
        return config.bulk_backend->coarse_fallback_reason(rate, clock, CoarseRoute::lag_groups).empty();
    return config.bulk_backend->permits_native_exact_chirp_z() && native_float_kernel() &&
           config.bulk_backend->coarse_fallback_reason(rate, clock, CoarseRoute::frequency_tiles).empty() &&
           !config.bulk_backend->coarse_fallback_reason(rate, clock, CoarseRoute::lag_groups).empty();
}

double chirp_fft_work(std::size_t lanes, std::size_t length) {
    return (2 * lanes + 1) * static_cast<double>(length) * std::log2(length);
}

bool enough_device_direct_work(const Plan& plan, std::size_t frequencies, double chirp_stages, unsigned workers) {
    const auto lanes = std::min<std::size_t>(workers, plan.workspaces());
    const auto direct_terms = static_cast<double>(plan.workspaces()) * plan.layout.kernel_rows * frequencies;
    // Use the measured geometry as a minimum device cost and a minimum
    // direct-term/FFT-stage ratio. As the native transform grows, direct
    // work must grow at least proportionally. An owner with fewer admitted
    // lanes needs more device work before preferring the native search.
    return direct_terms >= matched_direct_terms * std::max(1., chirp_stages / matched_chirp_stages) *
        std::max(1., matched_chirp_workers / lanes);
}

std::size_t lag_group_limit(const Plan& plan, const ReceiverConfig& config) {
    if (plan.source_step != 60 && !exact_chirp_z_profile(plan, config)) return 0;
    const auto lanes = 2 * plan.workspaces() * plan.ray_count;
    if (plan.layout.lag_count > max_fft_products / lanes) return 0;
    return max_fft_products / (lanes * plan.layout.lag_count);
}

std::size_t lag_point_limit(bool exact_chirp) {
    // The exact one-arrival native scorer has one uniform group. Extending
    // that convolution preserves the grid and avoids repeating the long
    // input transform in many small frequency chunks.
    return exact_chirp ? max_frequency_points : max_lag_frequency_points;
}

std::size_t lag_group_fft(const Layout& layout, std::size_t count) {
    return count > 1 ? fft_size(add(layout.kernel_rows, count - 1)) : 0;
}

constexpr std::size_t lag_convolution_rows(std::size_t kernel_rows, std::size_t points, std::size_t groups) {
    if (!groups || points <= 1) return 0;
    // Ordered disjoint or touching intervals share at most one endpoint per
    // boundary. Their total (group count minus one) cannot exceed points-1.
    // Every nonsingleton consumes at least one such step, and padding costs
    // less than twice each convolution's unpadded support.
    const auto steps = points - 1;
    return mul(2, add(mul(std::min(groups, steps), kernel_rows), steps));
}

// Two touching two-point intervals can need two transforms for three unique
// points. Any number of repeated singleton endpoints needs no convolution.
static_assert(lag_convolution_rows(4096, 3, 2) >= 2 * std::bit_ceil(std::size_t{4097}));
static_assert(lag_convolution_rows(4096, 1, 3) == 0);

double frequency_fft_work(const Plan& plan, std::size_t points) {
    return points * (1 + plan.workspaces() * plan.ray_count) * static_cast<double>(plan.layout.nfft) *
        std::log2(plan.layout.nfft);
}

bool prefer_exact_singleton(const Plan& plan, const ReceiverConfig& config) {
    return (!config.bulk_backend || config.bulk_backend->supports_exact_device_chirp_z()) &&
        4. * plan.workspaces() * plan.layout.kernel_rows <= frequency_fft_work(plan, 1);
}

bool exact_chirp_device_cost(const Plan& plan, const ReceiverConfig& config,
                             std::size_t frequencies, double stages, unsigned workers) {
    return !config.bulk_backend || config.bulk_backend->supports_exact_device_chirp_z() ||
           enough_device_direct_work(plan, frequencies, stages, workers);
}

bool choose_lag(const Plan& plan, const FrequencyGrid& grid, const ReceiverConfig& config, unsigned workers) {
    const auto limit = lag_group_limit(plan, config);
    const auto exact_chirp = exact_chirp_z_profile(plan, config);
    if (grid.values.size() > lag_point_limit(exact_chirp) || grid.groups.size() > limit) return false;
    const auto lanes = plan.workspaces() * plan.ray_count * plan.layout.lag_count;
    auto products = 2 * lanes * grid.groups.size();
    double work = 0;
    for (const auto& group : grid.groups) {
        if (const auto length = lag_group_fft(plan.layout, group.count)) {
            ++products;
            work += chirp_fft_work(lanes, length);
        } else {
            work += static_cast<double>(lanes) * plan.layout.kernel_rows;
        }
    }
    if (products > max_fft_products) return false;
    const auto forward = grid.values.size(), inverse = forward * plan.workspaces() * plan.ray_count;
    const double frequency_work = frequency_fft_work(plan, forward);
    if (exact_chirp) {
        if (forward == 1) return prefer_exact_singleton(plan, config);
        // The chirp-Z convolution uses two FP64 transforms per workspace
        // instead of one inverse transform for each frequency and workspace.
        // Require clear transform-work savings before changing from the
        // existing packed or floating frequency route.
        return 4 * work <= frequency_work && exact_chirp_device_cost(plan, config, forward, work, workers);
    }
    return inverse > max_fft_products || work < frequency_work;
}

bool compact_native_precision(const Plan& plan, const ReceiverConfig& config) {
    return plan.output_fs <= 10 && config.clock_bound_ppm > 0 && 2 * config.clock_bound_ppm <= .001;
}

bool single_ray_float_bank(const Plan& plan, const ReceiverConfig& config) {
    // The one-column Gram has ratio 1. This is the frequency-route bank
    // eligibility. A selected chirp-Z route retains time-domain columns.
    return plan.ray_count == 1 && plan.source_step == 1 && !config.bulk_backend &&
           config.native_coarse_precision != NativeCoarsePrecision::fp64 &&
           !compact_native_precision(plan, config) && native_float_kernel();
}

bool use_q15_products(NativeCoarsePrecision preference, bool fused_float) {
    // Each complex product is two bounded INT32 pair sums. The FP32 inverse
    // FFT and checked native transform/workspace bounds govern its length.
    return fused_float &&
           (preference == NativeCoarsePrecision::automatic || preference == NativeCoarsePrecision::q15);
}

std::size_t storage(std::size_t count, std::size_t complex_bytes = sizeof(Complex)) {
    return mul(count, complex_bytes);
}

std::size_t scoring_worker_workspace(const Layout& layout, const Plan& plan,
                                     std::size_t complex_bytes = sizeof(Complex)) {
    return add(storage(mul(layout.nfft, plan.ray_count), complex_bytes), mul(layout.lag_count, sizeof(double)));
}

struct WorkspaceQuote { std::size_t total = 0, bulk_host = 0; };

std::optional<CoarseFrequencyPlan> frequency_admission(const Plan& plan, const ReceiverConfig& config,
                                                       std::size_t frequency_count, unsigned workers) {
    if (!config.bulk_backend) return {};
    const auto rate = static_cast<double>(plan.output_fs), clock = 2 * config.clock_bound_ppm;
    if (!config.bulk_backend->coarse_fallback_reason(rate, clock, CoarseRoute::frequency_tiles).empty()) return {};
    const auto& layout = plan.layout;
    const auto width = tile_points(plan, frequency_count, workers);
    return config.bulk_backend->frequency_plan(
        {plan.ray_count, layout.kernel_rows, layout.nfft, layout.lag_count, frequency_count, rate, 0, width},
        plan.workspaces(), std::max({layout.nfft, plan.filter_fft, plan.fidelity_layout.nfft}));
}

WorkspaceQuote scratch_workspace(const Plan& plan, const ReceiverConfig& config,
                                 std::size_t frequency_count, unsigned workers) {
    require_coarse(workers > 0, "coarse receiver requires an explicit positive worker share");
    const auto& layout = plan.layout;
    const auto peak_bytes = arrival_peak_slots(layout.lag_count) * sizeof(Peak);
    const auto exact_chirp = exact_chirp_z_profile(plan, config);
    auto lag_points = std::min(frequency_count, lag_point_limit(exact_chirp));
    if (exact_chirp) {
        // Every nonsingleton convolution is at least as long as the
        // one-arrival frequency FFT. Combine that lower bound with the
        // maximum possible grid size when quoting either valid route.
        const auto minimum_chirp = chirp_fft_work(plan.workspaces(), layout.nfft);
        const auto exact_group_possible = 4 * minimum_chirp <= frequency_fft_work(plan, lag_points) &&
            exact_chirp_device_cost(plan, config, lag_points, minimum_chirp, workers);
        if (!exact_group_possible) lag_points = prefer_exact_singleton(plan, config) ? 1 : 0;
    }
    const auto lag_groups = !lag_points ? 0 :
        std::min(lag_group_limit(plan, config), config.helper_enabled ? max_intervals : 1);
    const auto lag_fft = lag_groups ? lag_group_fft(layout, lag_points) : 0;
    const bool fused_float = single_ray_float_bank(plan, config);
    const bool packed = use_q15_products(config.native_coarse_precision, fused_float);
    require_coarse(config.native_coarse_precision != NativeCoarsePrecision::q15 || packed,
                   "Q15 coarse products require the native exact single-ray AVX2 frequency route");
    const auto complex_bytes = fused_float ? sizeof(std::complex<float>) : sizeof(Complex);
    const auto spectrum_bytes = packed ? 2 * sizeof(std::int16_t) : complex_bytes;
    const auto nfft = std::max({layout.nfft, plan.filter_fft, plan.fidelity_layout.nfft, lag_fft});
    const auto phases = plan.source_step > 1 ? MappedProjection::phase_count(plan) : 0;
    // Common FFT twiddles, immutable FIR response(s), raw/collapsed taps and
    // bounded controller metadata. Input and the fine stage are external.
    auto projection = storage(add(fused_float ? plan.filter_fft : nfft, mul(1 + phases, plan.filter_fft)));
    projection = add(projection, mul(add(mul(2, plan.taps), 4 * plan.source_step), sizeof(double)));
    const auto base = add(mul(frequency_count, sizeof(double)), 128 * 1024);
    const auto float_twiddles = fused_float ? storage(layout.nfft, complex_bytes) : 0;
    auto bank = storage(mul(mul(plan.workspaces(), plan.ray_count), layout.nfft), spectrum_bytes);
    bank = add(bank, mul(plan.workspaces(), sizeof(Workspace)));
    const auto source = add(plan.source_geometry.source_workspace_bytes, PatternSource::render_bank_scratch_bytes);
    const auto input_rows = plan.source_step == 1 ? layout.source_rows : add(ceil_div(layout.raw_rows - 1, plan.source_step), 1);
    const auto raw_columns = storage(mul(plan.ray_count, layout.kernel_rows));
    auto bank_scratch = add(storage(plan.filter_fft), raw_columns);
    if (packed) {
        // FIR scratch ends before conversion. Before packing, the raw
        // column and FP32 FFT buffer coexist, with no Q15 destination yet.
        // During packing, the FP32 buffer and already charged Q15 bank
        // coexist, with the raw column released.
        const auto fp32_fft = storage(layout.nfft, complex_bytes);
        const auto conversion = add(raw_columns, storage(layout.nfft, complex_bytes - spectrum_bytes));
        bank_scratch = std::max({bank_scratch, conversion, fp32_fft});
    } else if (!fused_float) {
        // The original double destination may coexist with a raw column.
        bank_scratch = add(bank_scratch, storage(layout.nfft));
    }
    auto per_bank_worker = add(add(source, storage(input_rows)), bank_scratch);
    if (phases)
        per_bank_worker = add(per_bank_worker, storage(2 * mapped_edge_samples(plan, layout)));
    if (config.bulk_backend && phases)
        per_bank_worker = add(per_bank_worker, storage(layout.kernel_rows));
    const auto prep = add(bank, mul(std::min<std::size_t>(workers, lanes), per_bank_worker));
    const auto tile = tile_points(plan, frequency_count, workers);
    auto score = add(add(bank, storage(layout.crop_rows)), storage(mul(tile, layout.nfft), spectrum_bytes));
    score = add(score, mul(mul(frequency_count, plan.workspaces()), peak_bytes));
    const auto products = mul(std::min<std::size_t>(workers, mul(tile, plan.workspaces())),
                              scoring_worker_workspace(layout, plan, complex_bytes));
    // Observation packing and correlation are consecutive batches. The
    // first can hold at most one temporary FP32 spectrum per frequency task.
    const auto observation_packing = packed ? storage(mul(std::min<std::size_t>(tile, workers), layout.nfft), complex_bytes) : 0;
    score = add(score, std::max(products, observation_packing));
    score = add(score, mul(plan.workspaces(), retained_per_bit * sizeof(CoarseProposal)));
    const auto observation = add(bank, storage(add(layout.crop_rows, plan.filter_fft)));
    const auto preparation_base = add(projection, float_twiddles);
    const auto score_base = fused_float ? float_twiddles : projection;
    auto high = std::max(add(preparation_base, std::max(prep, observation)), add(score_base, score));
    if (lag_groups) {
        const auto zoom_plans = storage(add(mul(lag_groups, layout.kernel_rows),
            lag_convolution_rows(layout.kernel_rows, lag_points, lag_groups)));
        auto worker = storage(add(lag_fft, mul(plan.ray_count, lag_points)));
        worker = add(worker, mul(mul(lag_points, layout.lag_count), sizeof(double)));
        const auto lag_bank = exact_chirp
            ? add(mul(raw_columns, plan.workspaces()), mul(plan.workspaces(), sizeof(Workspace))) : bank;
        auto lag = add(lag_bank, storage(layout.crop_rows));
        lag = add(lag, zoom_plans);
        lag = add(lag, mul(std::min<std::size_t>(workers, plan.workspaces()), worker));
        lag = add(lag, mul(mul(lag_points, plan.workspaces()), peak_bytes));
        lag = add(lag, mul(plan.workspaces(), retained_per_bit * sizeof(CoarseProposal)));
        if (exact_chirp) {
            // A caller can supply a narrower valid interval than the full
            // prior. Reserve the maximum of the frequency plan above and
            // the time-domain chirp-Z plan with its own FP64 columns and FFT.
            const auto lag_bank_scratch = add(add(source, storage(input_rows)),
                                              add(storage(plan.filter_fft), raw_columns));
            const auto lag_prep = add(lag_bank, mul(std::min<std::size_t>(workers, lanes), lag_bank_scratch));
            const auto lag_observation = add(lag_bank, storage(add(layout.crop_rows, plan.filter_fft)));
            lag = std::max({lag, lag_prep, lag_observation});
        }
        const auto lag_projection = fused_float ? add(projection, storage(nfft - plan.filter_fft)) : projection;
        high = std::max(high, add(lag_projection, lag));
    }
    if (phases) {
        const auto& local = plan.fidelity_layout;
        const auto controls = storage(add(2 * local.nfft, add(local.crop_rows, plan.filter_fft)));
        const auto local_knots = add(ceil_div(local.raw_rows - 1, plan.source_step), 1);
        auto scratch = add(mul(2, source), storage(add(add(2 * local.source_rows, local_knots), plan.filter_fft)));
        scratch = add(scratch, storage(2 * mapped_edge_samples(plan, local)));
        scratch = add(scratch, storage(mul(6 * plan.ray_count, local.kernel_rows)));
        scratch = add(scratch, storage(mul(2 * plan.ray_count, local.nfft)));
        scratch = add(scratch, scoring_worker_workspace(local, plan));
        const auto fidelity = add(controls, mul(std::min<std::size_t>(workers, receiver_epoch_offsets.size()), scratch));
        high = std::max(high, add(projection, fidelity));
    }
    // Task metadata and the bounded active native call stack are charged
    // separately from shared immutable tables and algorithm buffers.
    const auto task_overhead = mul(std::min<std::size_t>(workers, tile * plan.workspaces()), 128 * 1024);
    const auto rate = static_cast<double>(plan.output_fs), clock = 2 * config.clock_bound_ppm;
    const bool bulk_frequency = config.bulk_backend &&
        config.bulk_backend->coarse_fallback_reason(rate, clock, CoarseRoute::frequency_tiles).empty();
    const bool bulk_lag = config.bulk_backend && lag_groups &&
        config.bulk_backend->coarse_fallback_reason(rate, clock, CoarseRoute::lag_groups).empty();
    // Shared compiled programs can remain live even for a native fallback.
    const auto persistent = config.bulk_backend ? config.bulk_backend->persistent_host_workspace() : 0;
    const auto frequency = frequency_admission(plan, config, frequency_count, workers);
    const auto legacy = bulk_lag || (bulk_frequency && !frequency) ? config.bulk_backend->coarse_host_bytes(
        bulk_lag ? nfft : std::max({layout.nfft, plan.filter_fft, plan.fidelity_layout.nfft}),
        std::max(bulk_frequency && !frequency ? tile : 0, bulk_lag ? lag_points : 0), layout.lag_count) : 0;
    const auto bulk_host = add(persistent, std::max(legacy, frequency ? frequency->host_workspace_bytes : 0));
    return {add(add(add(base, high), task_overhead), bulk_host), bulk_host};
}

CoarseWorkPlan make_work_plan(const Plan& geometry, const FrequencyGrid& grid,
                              const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                              unsigned workers, WorkspaceQuote workspace, std::size_t limit) {
    const auto& layout = geometry.layout;
    const bool lag = choose_lag(geometry, grid, config, workers);
    CoarseWorkPlan work;
    work.route = lag ? CoarseRoute::lag_groups : CoarseRoute::frequency_tiles;
    work.public_slot = slot.index;
    work.pattern_epoch = slot.format.epoch;
    work.symbol_ordinal = slot.format.symbol_ordinal;
    work.first_sample = slot.first_sample;
    work.stop_sample = slot.stop_sample;
    work.workers = workers;
    work.source_rows = layout.source_rows;
    work.raw_template_rows = layout.raw_rows;
    work.projected_template_rows = layout.kernel_rows;
    work.projected_observation_rows = layout.crop_rows;
    work.template_sampling_step = geometry.source_step;
    work.fir_taps = geometry.taps;
    work.fir_fft_rows = geometry.filter_fft;
    work.correlation_fft_rows = lag ? 0 : layout.nfft;
    if (lag)
        for (const auto& group : grid.groups)
            work.correlation_fft_rows = std::max(work.correlation_fft_rows, lag_group_fft(layout, group.count));
    work.frequency_groups = grid.groups.size();
    work.frequency_points = grid.values.size();
    work.arrival_points = layout.lag_count;
    work.workspaces = geometry.workspaces();
    work.rays = geometry.ray_count;
    work.clocks = geometry.clock_count;
    work.tile_width = lag ? std::min<std::size_t>(workers, work.workspaces) :
                            tile_points(geometry, work.frequency_points, workers);
    if (!lag && config.bulk_backend && config.bulk_backend->coarse_fallback_reason(
            static_cast<double>(geometry.output_fs), 2 * config.clock_bound_ppm, work.route).empty()) {
        // The Helper can narrow the executed grid, but the retained shape and
        // host/device charge must come from the same full public prior.
        work.bulk_frequency_admission = frequency_admission(geometry, config, frequency_bound(geometry, config), workers);
        if (const auto& admission = work.bulk_frequency_admission) {
            if (admission->native_fallback_reason.empty()) {
                require_coarse(admission->tile_width && admission->workspace_batch && admission->device_workspace_bytes,
                               "coarse backend returned an invalid automatic frequency admission");
                work.tile_width = std::min(work.tile_width, admission->tile_width);
            }
        } else {
            const auto width = config.bulk_backend->maximum_frequency_tile_width(
                {work.rays, layout.kernel_rows, layout.nfft, layout.lag_count, work.frequency_points,
                 static_cast<double>(geometry.output_fs), 0, work.tile_width},
                work.workspaces, work.tile_width, 2 * config.clock_bound_ppm);
            require_coarse(width && width <= work.tile_width, "coarse backend returned an invalid frequency tile width");
            work.tile_width = width;
        }
    }
    work.total_tiles = ceil_div(lag ? work.workspaces : work.frequency_points, work.tile_width);
    work.workspace_reserved_bytes = workspace.total;
    work.workspace_limit_bytes = limit;
    work.bulk_host_workspace_bytes = workspace.bulk_host;
    work.input_sample_rate_hz = config.sample_rate_hz;
    work.projected_sample_rate_hz = static_cast<double>(geometry.output_fs);
    work.maximum_frequency_step_hz = geometry.max_frequency_step;
    work.clock_gap_ppm = config.clock_bound_ppm;
    work.arrival_lower_seconds = slot.arrival_lower_seconds;
    work.arrival_upper_seconds = slot.arrival_upper_seconds;
    work.mapped_fidelity_native_fp64 = geometry.source_step > 1;
    return work;
}

template<class Fft>
CoarseOperationCounts score_frequency_tile(const Plan& plan, std::span<const Workspace> bank,
                                            std::span<const Complex> observation, std::span<const double> grid,
                                            std::span<Peak> peaks, std::size_t first, std::size_t count,
                                            double variance, const Fft& fft,
                                            unsigned workers, std::stop_token stop, ReceiverExecutor* executor,
                                            std::uint64_t public_slot, int observation_exponent = 0,
                                            bool pack = false) {
    const auto& layout = plan.layout;
    const auto peak_slots = arrival_peak_slots(layout.lag_count);
    const auto local_count = mul(grid.size(), peak_slots);
    const auto distance = std::max<std::size_t>(1, extent(std::ceil(plan.separation * plan.output_fs)));
    std::vector<std::vector<FftComplex<Fft>>> spectra(count);
    std::vector<native_fft::Q15Spectrum> packed_spectra(pack ? count : 0);
    std::vector<CoarseOperationCounts> spectrum_work(count);
    parallel(count, workers, stop, executor, public_slot, [&](std::size_t index, std::stop_token job_stop) {
        spectra[index] = observation_spectrum(layout, observation, grid[first + index], plan.output_fs,
                                               fft, spectrum_work[index], job_stop, observation_exponent);
        if constexpr (std::is_same_v<Fft, native_fft::FloatPlan>) {
            if (pack) {
                packed_spectra[index] = native_fft::pack_q15(spectra[index], job_stop);
                std::vector<std::complex<float>>().swap(spectra[index]);
            }
        }
    });
    const auto jobs = mul(bank.size(), count);
    const auto active = std::min<std::size_t>(workers, jobs);
    std::vector<CoarseOperationCounts> worker_work(active);
    parallel(active, workers, stop, executor, public_slot, [&](std::size_t task, std::stop_token job_stop) {
        auto& work = worker_work[task];
        for (auto index = task; index < jobs; index += active) {
            const auto workspace = index / count;
            const auto local_frequency = index % count;
            const auto frequency = first + local_frequency;
            const auto scores = correlation_scores<Fft>(plan, layout, bank[workspace], spectra[local_frequency],
                                                    variance, fft, work, job_stop, observation_exponent,
                                                    pack ? &packed_spectra[local_frequency] : nullptr);
            const auto arrivals = arrival_peaks(scores, distance, frequency);
            const auto selected = std::span(arrivals).first(peak_slots);
            std::copy(selected.begin(), selected.end(), peaks.begin() + workspace * local_count + frequency * peak_slots);
            for (const auto& peak : selected) if (std::isfinite(peak.score)) note(work.retained_peaks, 1);
        }
    });
    CoarseOperationCounts complete;
    for (const auto& work : spectrum_work) combine(complete, work);
    for (const auto& work : worker_work) combine(complete, work);
    return complete;
}

struct ZoomKernel {
    FrequencyGroup group;
    std::vector<Complex> input_phase, convolution;
};

std::vector<ZoomKernel> zoom_kernels(const Plan& plan, const FrequencyGrid& grid,
                                     const native_fft::DoublePlan* fft, CoarseOperationCounts& work,
                                     std::stop_token stop) {
    std::vector<ZoomKernel> kernels;
    kernels.reserve(grid.groups.size());
    const auto rows = plan.layout.kernel_rows;
    for (const auto& group : grid.groups) {
        const auto nfft = lag_group_fft(plan.layout, group.count);
        auto& kernel = kernels.emplace_back(ZoomKernel{
            group, std::vector<Complex>(rows), std::vector<Complex>(nfft)});
        const auto first = static_cast<long double>(group.lower) / plan.output_fs;
        const auto step = group.count > 1 ?
            (static_cast<long double>(group.upper) - group.lower) / ((group.count - 1.L) * plan.output_fs) : 0;
        for (std::size_t i = 0; i < rows; ++i) {
            if ((i & 4095U) == 0) check_stop(stop);
            const auto n = static_cast<long double>(i);
            kernel.input_phase[i] = rotation(-first * n - .5L * step * n * n);
        }
        if (!nfft) continue;
        for (std::size_t i = 0; i < group.count; ++i) {
            if ((i & 4095U) == 0) check_stop(stop);
            const auto n = static_cast<long double>(i);
            kernel.convolution[i] = rotation(.5L * step * n * n);
        }
        for (std::size_t i = 1; i < rows; ++i) {
            if ((i & 4095U) == 0) check_stop(stop);
            const auto n = static_cast<long double>(i);
            kernel.convolution[nfft - i] = rotation(.5L * step * n * n);
        }
        if (fft) tracked_fft(kernel.convolution, false, *fft, work, stop);
    }
    return kernels;
}

void correlate_lag_group(std::span<const Complex> window, std::span<const Complex> column,
                         const ZoomKernel& kernel, std::span<Complex> products, std::span<Complex> output,
                         const native_fft::DoublePlan& fft, CoarseOperationCounts& work,
                         std::stop_token stop) {
    auto row = output.subspan(kernel.group.first, kernel.group.count);
    if (kernel.convolution.empty()) {
        Complex score{};
        for (std::size_t i = 0; i < window.size(); ++i) {
            if ((i & 4095U) == 0) check_stop(stop);
            score += window[i] * std::conj(column[i]) * kernel.input_phase[i];
        }
        row[0] = score;
        note(work.singleton_dot_terms, window.size());
        return;
    }
    auto padded = products.first(kernel.convolution.size());
    for (std::size_t i = 0; i < window.size(); ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        padded[i] = window[i] * std::conj(column[i]) * kernel.input_phase[i];
    }
    std::fill(padded.begin() + window.size(), padded.end(), Complex{});
    tracked_fft(padded, false, fft, work, stop);
    for (std::size_t i = 0; i < padded.size(); ++i) {
        if ((i & 4095U) == 0) check_stop(stop);
        padded[i] *= kernel.convolution[i];
    }
    tracked_fft(padded, true, fft, work, stop);
    // The omitted output chirp and lag-origin rotation multiply both rays
    // by the same unit phase at each CFO. The Hermitian score cancels it.
    std::copy_n(padded.begin(), row.size(), row.begin());
}

CoarseOperationCounts score_lag_tile(const Plan& plan, std::span<const Workspace> bank,
                                     std::span<const Complex> observation, std::span<const ZoomKernel> kernels,
                                     std::size_t frequencies, std::size_t largest_fft, std::span<Peak> peaks,
                                     double variance, const native_fft::DoublePlan& fft,
                                     unsigned workers, std::stop_token stop, ReceiverExecutor* executor,
                                     std::uint64_t public_slot) {
    const auto& layout = plan.layout;
    const auto peak_slots = arrival_peak_slots(layout.lag_count);
    const auto local_count = mul(frequencies, peak_slots);
    const auto distance = std::max<std::size_t>(1, extent(std::ceil(plan.separation * plan.output_fs)));
    std::vector<CoarseOperationCounts> worker_work(bank.size());
    parallel(bank.size(), workers, stop, executor, public_slot, [&](std::size_t index, std::stop_token job_stop) {
        auto& work = worker_work[index];
        std::vector<Complex> products(largest_fft);
        Columns correlations;
        for (std::size_t ray = 0; ray < plan.ray_count; ++ray) correlations[ray].resize(frequencies);
        std::vector<double> scores(mul(frequencies, layout.lag_count));
        const auto& workspace = bank[index];
        for (std::size_t lag = 0; lag < layout.lag_count; ++lag) {
            check_stop(job_stop);
            const auto window = observation.subspan(lag, layout.kernel_rows);
            for (std::size_t ray = 0; ray < plan.ray_count; ++ray)
                for (const auto& kernel : kernels)
                    correlate_lag_group(window, workspace.columns[ray], kernel, products, correlations[ray],
                                        fft, work, job_stop);
            for (std::size_t f = 0; f < frequencies; ++f) {
                if ((f & 4095U) == 0) check_stop(job_stop);
                const auto second = plan.ray_count == 2 ? correlations[1][f] : Complex{};
                const auto score = quadratic(workspace.inverse.matrix, correlations[0][f], second) / variance;
                require_coarse(std::isfinite(score), "nonfinite coarse lag correlation score");
                scores[f * layout.lag_count + lag] = score;
            }
        }
        for (std::size_t f = 0; f < frequencies; ++f) {
            if ((f & 4095U) == 0) check_stop(job_stop);
            const auto arrivals = arrival_peaks(std::span(scores).subspan(f * layout.lag_count, layout.lag_count), distance, f);
            const auto selected = std::span(arrivals).first(peak_slots);
            std::copy(selected.begin(), selected.end(), peaks.begin() + index * local_count + f * peak_slots);
            for (const auto& peak : selected) if (std::isfinite(peak.score)) note(work.retained_peaks, 1);
        }
        note(work.workspace_frequency_pairs, frequencies);
        note(work.score_rows, scores.size());
    });
    CoarseOperationCounts complete;
    for (const auto& work : worker_work) combine(complete, work);
    return complete;
}

void retain_proposals(CoarseResult& result, const Plan& plan, const ReceiverSlotPlan& slot,
                      const std::vector<Workspace>& bank, std::span<Peak> peaks, std::span<const double> grid,
                      unsigned workers, std::stop_token stop, ReceiverExecutor* executor) {
    using Retained = std::array<CoarseProposal, retained_per_bit>;
    std::vector<Retained> retained(bank.size());
    const auto local_count = mul(grid.size(), arrival_peak_slots(plan.layout.lag_count));
    parallel(bank.size(), workers, stop, executor, slot.index, [&](std::size_t workspace, std::stop_token job_stop) {
        auto local = peaks.subspan(workspace * local_count, local_count);
        // Keep the original CFO-major order on equal scores. That order is
        // independent of worker completion and of the selected tile width.
        std::sort(local.begin(), local.end(), [](const Peak& a, const Peak& b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.frequency != b.frequency) return a.frequency < b.frequency;
            return a.lag < b.lag;
        });
        check_stop(job_stop);
        auto& out = retained[workspace];
        for (auto& row : out) row.score = -infinity;
        const auto bit = static_cast<std::uint8_t>(workspace % 2);
        const auto epoch = workspace / (2 * plan.clock_count);
        const auto clock = workspace / 2 % plan.clock_count;
        std::size_t count = 0;
        for (const auto& peak : local) {
            if (!std::isfinite(peak.score)) break;
            const auto arrival = slot.arrival_lower_seconds + static_cast<double>(peak.lag) / plan.output_fs;
            const auto frequency = grid[peak.frequency];
            if (count && std::abs(arrival - out[0].coordinate.arrival_seconds) < plan.separation &&
                std::abs(frequency - out[0].coordinate.frequency_hz) < 2 * plan.max_frequency_step) continue;
            out[count++] = {{receiver_epoch_offsets[epoch], arrival, frequency, plan.clocks[clock]},
                             peak.score, bit, bank[workspace].inverse.rank};
            if (count == retained_per_bit) break;
        }
    });
    for (std::size_t bit = 0; bit < 2; ++bit) {
        Retained best;
        for (auto& row : best) row.score = -infinity;
        for (std::size_t i = bit; i < retained.size(); i += 2)
            for (const auto& row : retained[i]) {
                if (row.score > best[0].score) {
                    best[1] = best[0];
                    best[0] = row;
                } else if (row.score > best[1].score) {
                    best[1] = row;
                }
            }
        if (!std::isfinite(best[0].score)) {
            result.proposal_count = 0;
            result.reason = "coarse public search did not retain proposals for both bits";
            return;
        }
        for (const auto& row : best) if (std::isfinite(row.score)) result.proposals[result.proposal_count++] = row;
    }
}

void refine(CoarseResult& result, const Plan& plan, const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const auto half_step = config.clock_bound_ppm / 2;
    const auto center = slot.source_start_seconds + .5L * plan.layout.source_rows / plan.fs;
    using Token = std::tuple<std::int64_t, double, double, double>;
    std::array<Token, 3 * 2 * retained_per_bit> tokens{};
    result.coordinates.reserve(tokens.size());
    for (std::size_t i = 0; i < result.proposal_count; ++i) {
        const auto& coordinate = result.proposals[i].coordinate;
        const auto old_clock = coordinate.clock_ppm;
        const auto old_scale = 1 + old_clock * 1e-6;
        for (const auto delta : {-half_step, 0., half_step}) {
            const auto clock = std::clamp(old_clock + delta, -config.clock_bound_ppm, config.clock_bound_ppm);
            // Adjust the arrival to keep the same DATA midpoint after the
            // candidate's clock changes. Avoid subtracting large UTC times.
            const auto correction = center * ((clock - old_clock) * 1e-6) / (old_scale * (1 + clock * 1e-6));
            const auto arrival = std::clamp(static_cast<double>(coordinate.arrival_seconds + correction),
                                           slot.arrival_lower_seconds, slot.arrival_upper_seconds);
            const Token token{coordinate.epoch_offset_seconds, std::round(arrival * 1e12),
                              std::round(coordinate.frequency_hz * 1e12), std::round(clock * 1e9)};
            const auto end = tokens.begin() + result.coordinates.size();
            if (std::find(tokens.begin(), end, token) != end) continue;
            *end = token;
            result.coordinates.push_back({coordinate.epoch_offset_seconds, arrival, coordinate.frequency_hz, clock});
        }
    }
}

} // namespace

CoarseArrivalPeaks select_coarse_arrivals(std::span<const double> scores, std::size_t separation) {
    require_coarse(!scores.empty() && separation > 0, "invalid coarse arrival peak geometry");
    if (scores.size() == 1) return {{0, 0}, 1};
    const auto visit_interiors = [&](auto visit) {
        for (std::size_t i = 1; i + 1 < scores.size();) {
            if (!(scores[i - 1] < scores[i])) {
                ++i;
                continue;
            }
            auto end = i + 1;
            while (end + 1 < scores.size() && scores[end] == scores[i]) ++end;
            if (scores[end] < scores[i] && visit(i + (end - i - 1) / 2)) return true;
            i = end;
        }
        return false;
    };
    const auto better = [&](std::size_t a, std::size_t b) {
        return scores[a] == scores[b] ? a > b : scores[a] > scores[b];
    };
    // Greedy distance suppression in descending score, then descending lag.
    // Only the two best surviving interiors can enter the final frontier.
    std::array<std::size_t, 4> choices{};
    std::size_t interior_count = 0;
    const auto available = [&](std::size_t index) {
        for (std::size_t i = 0; i < interior_count; ++i)
            if (std::max(index, choices[i]) - std::min(index, choices[i]) < separation) return false;
        return true;
    };
    while (interior_count < retained_per_bit) {
        auto best = scores.size();
        visit_interiors([&](std::size_t index) {
            if (available(index) && (best == scores.size() || better(index, best))) best = index;
            return false;
        });
        if (best == scores.size()) break;
        choices[interior_count++] = best;
    }
    // The endpoint peaks have independent eligibility. Equal-score ranking
    // uses the same higher-lag priority as the interior suppression step.
    std::size_t count = interior_count;
    if (scores[0] >= scores[1]) choices[count++] = 0;
    if (scores.back() >= scores[scores.size() - 2]) choices[count++] = scores.size() - 1;
    const bool clipped = count > retained_per_bit ||
        (count == retained_per_bit && interior_count == retained_per_bit && visit_interiors(available));
    const auto sort_choices = [&](auto less) {
        for (std::size_t i = 1; i < choices.size() && i < count; ++i)
            for (auto j = i; j > 0 && less(choices[j], choices[j - 1]); --j)
                std::swap(choices[j], choices[j - 1]);
    };
    if (clipped) {
        sort_choices(better);
        return {{choices[1], choices[0]}, retained_per_bit};
    }
    sort_choices(std::less<>{});
    return {{choices[0], choices[1]}, count};
}

std::size_t coarse_workspace(const ReceiverConfig& config, const ReceiverSlotPlan& slot, unsigned workers) {
    const auto plan = make_plan(config, slot);
    return scratch_workspace(plan, config, frequency_bound(plan, config), workers).total;
}

CoarseWorkPlan coarse_work_plan(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                std::span<const FrequencyInterval> intervals, unsigned workers) {
    const auto geometry = make_plan(config, slot);
    const auto workspace = scratch_workspace(geometry, config, frequency_bound(geometry, config), workers);
    const auto grid = frequencies(geometry, config, intervals);
    return make_work_plan(geometry, grid, config, slot, workers, workspace, workspace.total);
}

CoarseFineGeometry coarse_fine_geometry(const ReceiverConfig& config, const ReceiverSlotPlan& slot) {
    const auto plan = make_plan(config, slot);
    return {static_cast<double>(plan.output_fs), plan.max_frequency_step, config.clock_bound_ppm};
}

struct CoarseSession::Impl {
    struct PropagateStop {
        std::stop_source& cancellation;
        void operator()() const noexcept { cancellation.request_stop(); }
    };

    const ReceiverConfig config;
    const ReceiverSlotPlan slot;
    Plan geometry;
    FrequencyGrid grid;
    CoarseProgress receipt;
    std::unique_ptr<native_fft::DoublePlan> fft;
    std::unique_ptr<native_fft::FloatPlan> float_fft;
    std::unique_ptr<CoarseBulk> bulk;
    CoarseBulkCompletedWork bulk_completed;
    std::vector<Workspace> bank;
    std::vector<Complex> observation;
    std::vector<ZoomKernel> kernels;
    std::vector<Peak> peaks;
    std::optional<CoarseResult> result;
    double variance = 0;
    bool q15_products = false;
    std::size_t cursor = 0;
    ReceiverExecutor* executor;
    std::stop_source cancellation;
    std::stop_callback<PropagateStop> external_stop;

    Impl(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
         std::span<const Complex> iq, std::span<const FrequencyInterval> intervals,
         unsigned workers, std::size_t limit, std::stop_token outer_stop, ReceiverExecutor* executor)
        : config(config), slot(slot), executor(executor), external_stop(outer_stop, PropagateStop{cancellation}) {
        const auto began = PhaseClock::now();
        const auto stop = cancellation.get_token();
        check_stop(stop);
        require_coarse(config.native_coarse_precision == NativeCoarsePrecision::automatic ||
                       config.native_coarse_precision == NativeCoarsePrecision::fp64 ||
                       config.native_coarse_precision == NativeCoarsePrecision::q15 ||
                       config.native_coarse_precision == NativeCoarsePrecision::fp32,
                       "invalid native coarse arithmetic policy");
        geometry = make_plan(config, slot);
        const auto& layout = geometry.layout;
        require_coarse(iq.size() == capture_samples(slot), "coarse receiver input does not match its public window");
        const auto workspace = scratch_workspace(geometry, config, frequency_bound(geometry, config), workers);
        if (workspace.total > limit) throw ReceiverWorkspaceError("complete coarse receiver exceeds its admitted workspace");
        grid = frequencies(geometry, config, intervals);
        receipt.plan = make_work_plan(geometry, grid, config, slot, workers, workspace, limit);
        auto& work = receipt.plan;
        const bool lag = work.route == CoarseRoute::lag_groups;

        // Check every borrowed sample, including those beyond the finite crop.
        for (std::size_t i = 0; i < iq.size(); ++i) {
            if ((i & (chunk_samples - 1)) == 0) check_stop(stop);
            require_coarse(std::isfinite(iq[i].real()) && std::isfinite(iq[i].imag()), "coarse receiver requires finite input");
        }
        if (config.bulk_backend) {
            receipt.native_fallback_reason = config.bulk_backend->coarse_fallback_reason(
                work.projected_sample_rate_hz, 2 * config.clock_bound_ppm, work.route);
            if (receipt.native_fallback_reason.empty() && work.bulk_frequency_admission)
                receipt.native_fallback_reason = work.bulk_frequency_admission->native_fallback_reason;
            if (!receipt.native_fallback_reason.empty())
                config.bulk_backend->record_native_coarse(receipt.native_fallback_reason);
        }
        if (config.bulk_backend && receipt.native_fallback_reason.empty()) {
            const auto setup = PhaseClock::now();
            bulk = config.bulk_backend->make_coarse(work.projected_sample_rate_hz, 2 * config.clock_bound_ppm);
            require_coarse(static_cast<bool>(bulk), "requested coarse bulk backend did not create an execution session");
            receipt.filter_setup.wall_seconds = elapsed(setup);
            if (geometry.source_step > 1) {
                // This small current-address interpolation gate deliberately
                // stays independent FP64 native work. The full operational
                // bank and observation below use the selected bulk backend.
                const auto fidelity = PhaseClock::now();
                auto& operations = receipt.mapped_fidelity.operations;
                const native_fft::DoublePlan reference_fft(std::max(geometry.filter_fft, geometry.fidelity_layout.nfft), stop);
                const Projection reference(geometry, &reference_fft, nullptr, operations, stop);
                const MappedProjection mapped(geometry, reference, &reference_fft, operations, stop);
                validate_mapped_profile(geometry, config, slot, key, reference, mapped, reference_fft,
                                        workers, operations, stop, executor);
                receipt.mapped_fidelity.wall_seconds = elapsed(fidelity);
            }
        }
        const bool compact_precision = compact_native_precision(geometry, config);
        const bool qualified_cpu = native_float_kernel();
        const bool fused_float = !lag && single_ray_float_bank(geometry, config);
        q15_products = use_q15_products(config.native_coarse_precision, fused_float);
        const bool split_bank = !bulk && !lag && config.native_coarse_precision != NativeCoarsePrecision::fp64 &&
                                !compact_precision && qualified_cpu && !fused_float;
        {
            const auto setup = PhaseClock::now();
            if (!bulk || !bulk->supports_fir()) {
                const auto fft_rows = fused_float ? geometry.filter_fft :
                    std::max({layout.nfft, geometry.filter_fft, geometry.fidelity_layout.nfft, work.correlation_fft_rows});
                fft = std::make_unique<native_fft::DoublePlan>(fft_rows, stop);
            }
            if (bulk && bulk->supports_fir()) {
                const auto mapped_filters = geometry.source_step > 1 ? MappedProjection::phase_count(geometry) : 0;
                bulk->begin_fir_projection(geometry.filter_fft, 1 + mapped_filters, layout.crop_rows, stop);
            }
            const Projection projection(geometry, fft.get(), bulk && bulk->supports_fir() ? bulk.get() : nullptr,
                                        receipt.filter_setup.operations, stop);
            variance = projection.variance(config.noise_variance);
            require_coarse(std::isfinite(variance) && variance > 0, "coarse projected noise variance is invalid");
            std::optional<MappedProjection> mapped;
            if (geometry.source_step > 1)
                mapped.emplace(geometry, projection, fft.get(), receipt.filter_setup.operations, stop);
            receipt.filter_setup.wall_seconds += elapsed(setup);
            if (mapped && !bulk) {
                const auto fidelity = PhaseClock::now();
                validate_mapped_profile(geometry, config, slot, key, projection, *mapped, *fft,
                                        workers, receipt.mapped_fidelity.operations, stop, executor);
                receipt.mapped_fidelity.wall_seconds = elapsed(fidelity);
            }
            const auto bank_started = PhaseClock::now();
            if (fused_float) float_fft = std::make_unique<native_fft::FloatPlan>(layout.nfft, stop);
            bank = prepare_bank(geometry, config, slot, key, projection, mapped ? &*mapped : nullptr,
                                !bulk && !lag && !split_bank && !fused_float ? fft.get() : nullptr,
                                float_fft.get(), q15_products, workers,
                                receipt.template_bank.operations, stop, executor);
            receipt.template_bank.wall_seconds = elapsed(bank_started);
            mapped.reset();
            const auto input_started = PhaseClock::now();
            observation = projection.apply(iq.size(), layout.crop_first, layout.crop_rows,
                [&](std::size_t first, std::span<Complex> values) {
                    std::copy_n(iq.begin() + first, values.size(), values.begin());
                }, receipt.observation.operations, stop);
            receipt.observation.wall_seconds = elapsed(input_started);
        }
        if (fused_float) fft.reset();
        work.minimum_eigenvalue_ratio = 1;
        for (const auto& workspace : bank)
            work.minimum_eigenvalue_ratio = std::min(work.minimum_eigenvalue_ratio, workspace.eigenvalue_ratio);
        if (bulk) {
            work.fft_precision = "bulk";
            work.precision_reason = "compiled-backend";
        } else if (lag) {
            work.precision_reason = exact_chirp_z_profile(geometry, config)
                ? (work.frequency_points == 1 ? "exact-one-arrival-singleton-dot" : "exact-one-arrival-chirp-z")
                : "lag-groups";
        } else {
            // Selection uses public geometry and template conditioning, never
            // a decoded bit or observed score. Keep the original FP64 scorer.
            if (config.native_coarse_precision == NativeCoarsePrecision::fp64)
                work.precision_reason = "explicit-fp64";
            else if (compact_precision)
                work.precision_reason = "compact-tight-clock";
            else if (!qualified_cpu)
                work.precision_reason = "cpu-kernel-unqualified";
            else if (work.minimum_eigenvalue_ratio < 1. / 16)
                work.precision_reason = "ill-conditioned-template";
            else {
                work.fft_precision = "fp32";
                work.precision_reason = "qualified-post-filter-fft";
                if (q15_products) work.precision_reason = "q15-products-fp32-fft";
                work.observation_scale_exponent = float_scale_exponent(maximum_component(observation));
            }
            if (split_bank) {
                const auto transform_started = PhaseClock::now();
                if (work.fft_precision == "fp32") {
                    // FIR and independent mapped-fidelity work are finished. Do
                    // not keep an unused double twiddle table alongside the float
                    // table: the original larger native quote covers either one.
                    fft.reset();
                    float_fft = std::make_unique<native_fft::FloatPlan>(layout.nfft, stop);
                }
                std::array<CoarseOperationCounts, lanes> transform_work{};
                parallel(lanes, workers, stop, executor, slot.index, [&](std::size_t lane, std::stop_token job_stop) {
                    for (std::size_t clock = 0; clock < geometry.clock_count; ++clock) {
                        auto& workspace = bank[geometry.workspace_index(lane, clock)];
                        if (float_fft) to_float_spectrum(workspace, layout, *float_fft, transform_work[lane], job_stop);
                        else to_spectrum(workspace.columns, layout, *fft, transform_work[lane], job_stop);
                    }
                });
                for (const auto& operations : transform_work) combine(receipt.template_bank.operations, operations);
                receipt.template_bank.wall_seconds += elapsed(transform_started);
            }
            if (float_fft)
                for (const auto& workspace : bank) work.scaled_template_workspaces += workspace.scale_exponent != 0;
        }
        if (lag) {
            const auto lag_started = PhaseClock::now();
            kernels = zoom_kernels(geometry, grid, fft.get(), receipt.lag_setup.operations, stop);
            receipt.lag_setup.wall_seconds = elapsed(lag_started);
        }
        if (bulk) prepare_bulk(stop);
        peaks.resize(mul(mul(bank.size(), grid.values.size()), arrival_peak_slots(layout.lag_count)));
        refresh_retained_bytes();
        check_stop(stop);
        receipt.preparation_seconds = elapsed(began);
    }

    void prepare_bulk(std::stop_token stop) {
        const auto started = PhaseClock::now();
        const auto& plan = receipt.plan;
        std::vector<CoarseBulkWorkspace> columns;
        columns.reserve(bank.size());
        for (const auto& workspace : bank) {
            const auto& inverse = workspace.inverse.matrix;
            columns.push_back({{workspace.columns[0], workspace.columns[1]}, inverse.a, inverse.d, inverse.b});
        }
        std::vector<CoarseZoomGroup> zoom;
        zoom.reserve(kernels.size());
        for (const auto& kernel : kernels)
            zoom.push_back({kernel.group.first, kernel.group.count, kernel.input_phase, kernel.convolution});
        CoarseBulkWork operations;
        const auto& admission = plan.bulk_frequency_admission;
        bulk->prepare({plan.rays, plan.projected_template_rows, plan.correlation_fft_rows,
                       plan.arrival_points, plan.frequency_points, plan.projected_sample_rate_hz, variance,
                       admission ? admission->tile_width : plan.tile_width,
                       admission ? admission->workspace_batch : 0, admission ? admission->device_workspace_bytes : 0},
                      observation, columns, zoom, operations, stop);
        if (const auto reason = bulk->precision_reason(); !reason.empty())
            receipt.plan.precision_reason = reason;
        bulk_completed = bulk->completed_work();
        combine(receipt.bulk_prepare.operations, operations);
        // Preparation synchronously transfers the bank; final native NMS
        // retains only ranks and the full address/peak frontier on the host.
        for (auto& workspace : bank)
            for (auto& column : workspace.columns) std::vector<Complex>().swap(column);
        std::vector<Complex>().swap(observation);
        std::vector<ZoomKernel>().swap(kernels);
        receipt.bulk_prepare.wall_seconds = elapsed(started);
    }

    CoarseOperationCounts score_bulk(std::size_t first, std::size_t count, std::stop_token stop) {
        const auto& plan = receipt.plan;
        const bool lag = plan.route == CoarseRoute::lag_groups;
        const auto first_workspace = lag ? first : 0;
        const auto stop_workspace = lag ? add(first, count) : plan.workspaces;
        const auto first_frequency = lag ? 0 : first;
        const auto frequencies = lag ? plan.frequency_points : count;
        const auto peak_slots = arrival_peak_slots(plan.arrival_points);
        const auto row_width = mul(plan.frequency_points, peak_slots);
        const auto scores_per_workspace = mul(frequencies, plan.arrival_points);
        const auto distance = std::max<std::size_t>(1, extent(std::ceil(geometry.separation * geometry.output_fs)));
        auto next_workspace = first_workspace;
        CoarseOperationCounts completed;
        const auto sink = [&](std::size_t workspace, std::size_t rows, std::span<const double> scores) {
            check_stop(stop);
            require_coarse(workspace == next_workspace && rows > 0 && rows <= stop_workspace - next_workspace &&
                           scores.size() == mul(rows, scores_per_workspace),
                           "coarse bulk returned scores outside the requested canonical grid");
            for (std::size_t row = 0; row < rows; ++row) {
                for (std::size_t f = 0; f < frequencies; ++f) {
                    const auto values = scores.subspan(add(mul(row, scores_per_workspace), mul(f, plan.arrival_points)),
                                                       plan.arrival_points);
                    require_coarse(std::ranges::all_of(values, [](double x) { return std::isfinite(x); }),
                                   "nonfinite coarse bulk correlation score");
                    const auto frequency = first_frequency + f;
                    const auto arrivals = arrival_peaks(values, distance, frequency);
                    const auto selected = std::span(arrivals).first(peak_slots);
                    std::copy(selected.begin(), selected.end(), peaks.begin() +
                              (workspace + row) * row_width + frequency * peak_slots);
                    for (const auto& peak : selected) if (std::isfinite(peak.score)) note(completed.retained_peaks, 1);
                }
            }
            next_workspace += rows;
        };
        const auto peak_sink = [&](std::size_t workspace, std::size_t rows,
                                   std::span<const CoarseBulkPeaks> frontiers) {
            check_stop(stop);
            require_coarse(workspace == next_workspace && rows > 0 && rows <= stop_workspace - next_workspace &&
                           frontiers.size() == mul(rows, frequencies),
                           "coarse bulk returned frontiers outside the requested canonical grid");
            for (std::size_t row = 0; row < rows; ++row) {
                for (std::size_t f = 0; f < frequencies; ++f) {
                    const auto& frontier = frontiers[row * frequencies + f];
                    require_coarse(frontier.count > 0 && frontier.count <= peak_slots,
                                   "invalid coarse bulk frontier count");
                    const auto frequency = first_frequency + f;
                    std::array<Peak, retained_per_bit> selected{};
                    for (std::size_t i = 0; i < frontier.count; ++i) {
                        require_coarse(frontier.lags[i] < plan.arrival_points && std::isfinite(frontier.scores[i]) &&
                                       (!i || frontier.lags[0] != frontier.lags[i]),
                                       "invalid coarse bulk frontier arrival or score");
                        selected[i] = {frontier.scores[i], frequency, static_cast<std::size_t>(frontier.lags[i])};
                    }
                    std::copy_n(selected.begin(), peak_slots, peaks.begin() +
                                (workspace + row) * row_width + frequency * peak_slots);
                    note(completed.retained_peaks, frontier.count);
                }
            }
            next_workspace += rows;
        };
        CoarseBulkWork operations;
        if (lag) bulk->lag_tile(first, count, sink, operations, stop);
        else {
            const auto values = std::span(grid.values).subspan(first, count);
            if (!bulk->frequency_peaks(values, distance, peak_sink, operations, stop)) {
                const auto untouched = bulk->completed_work();
                require_coarse(next_workspace == first_workspace && operations == CoarseBulkWork{} &&
                               untouched.score_rows == bulk_completed.score_rows &&
                               untouched.executed == bulk_completed.executed,
                               "unsupported coarse frontier path changed work before fallback");
                bulk->frequency_tile(values, sink, operations, stop);
            }
        }
        const auto expected_pairs = mul(stop_workspace - first_workspace, frequencies);
        const auto expected_scores = mul(expected_pairs, plan.arrival_points);
        require_coarse(next_workspace == stop_workspace && operations.workspace_frequency_pairs == expected_pairs &&
                       operations.score_rows == expected_scores,
                       "coarse bulk did not score every requested public workspace and frequency");
        // The returned shape and work argument are separate from a fresh
        // backend completion witness. Setup or an earlier tile can already
        // have set executed, so require this tile's exact score-row delta.
        const auto execution = bulk->completed_work();
        require_coarse(execution.executed && execution.score_rows >= bulk_completed.score_rows &&
                       execution.score_rows - bulk_completed.score_rows == expected_scores,
                       "coarse bulk tile lacks fresh execution evidence for its score rows");
        bulk_completed = execution;
        combine(completed, operations);
        return completed;
    }

    CoarseState state() const noexcept {
        if (cancellation.stop_requested() && receipt.state != CoarseState::finished && receipt.state != CoarseState::failed)
            return CoarseState::cancelled;
        return receipt.state;
    }

    void refresh_retained_bytes() {
        auto bytes = add(sizeof(*this), fft ? fft->working_bytes() : 0);
        bytes = add(bytes, float_fft ? float_fft->working_bytes() : 0);
        bytes = add(bytes, mul(grid.values.capacity(), sizeof(double)));
        bytes = add(bytes, mul(grid.groups.capacity(), sizeof(FrequencyGroup)));
        bytes = add(bytes, mul(bank.capacity(), sizeof(Workspace)));
        for (const auto& workspace : bank)
            for (const auto& column : workspace.columns) bytes = add(bytes, storage(column.capacity()));
        for (const auto& workspace : bank)
            for (const auto& column : workspace.float_columns)
                bytes = add(bytes, mul(column.capacity(), sizeof(std::complex<float>)));
        for (const auto& workspace : bank)
            bytes = add(bytes, workspace.packed_column.working_bytes());
        bytes = add(bytes, storage(observation.capacity()));
        bytes = add(bytes, mul(kernels.capacity(), sizeof(ZoomKernel)));
        for (const auto& kernel : kernels) {
            bytes = add(bytes, storage(kernel.input_phase.capacity()));
            bytes = add(bytes, storage(kernel.convolution.capacity()));
        }
        bytes = add(bytes, mul(peaks.capacity(), sizeof(Peak)));
        if (result) {
            bytes = add(bytes, mul(result->coordinates.capacity(), sizeof(FineCoordinate)));
            bytes = add(bytes, result->reason.capacity() + 1);
        }
        if (bytes > receipt.plan.workspace_reserved_bytes)
            throw ReceiverWorkspaceError("retained coarse session exceeds its admitted workspace");
        receipt.retained_bytes = bytes;
    }
};

CoarseSession::CoarseSession(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                             std::span<const Complex> iq, std::span<const FrequencyInterval> intervals,
                             unsigned workers, std::size_t limit, std::stop_token stop, ReceiverExecutor* executor)
    : impl_(std::make_unique<Impl>(config, slot, key, iq, intervals, workers, limit, stop, executor)) {}

CoarseSession::~CoarseSession() = default;
CoarseSession::CoarseSession(CoarseSession&&) noexcept = default;
CoarseSession& CoarseSession::operator=(CoarseSession&&) noexcept = default;

CoarseProgress CoarseSession::progress() const {
    auto result = impl_->receipt;
    result.state = impl_->state();
    if (impl_->bulk) result.bulk = impl_->bulk->receipt();
    return result;
}

CoarseState CoarseSession::state() const noexcept { return impl_->state(); }

std::span<const double> CoarseSession::frequency_grid() const { return impl_->grid.values; }

void CoarseSession::cancel() noexcept {
    if (impl_) impl_->cancellation.request_stop();
}

void CoarseSession::advance(std::size_t maximum_tiles) {
    require_coarse(maximum_tiles > 0 && maximum_tiles <= 64, "coarse advance requires 1..64 tiles");
    auto& impl = *impl_;
    auto& receipt = impl.receipt;
    require_coarse(receipt.state != CoarseState::finished && receipt.state != CoarseState::failed,
                   "coarse session cannot advance from its terminal state");
    const auto stop = impl.cancellation.get_token();
    check_stop(stop);
    const auto& plan = receipt.plan;
    const bool lag = plan.route == CoarseRoute::lag_groups;
    const auto extent = lag ? plan.workspaces : plan.frequency_points;
    const auto began = PhaseClock::now();
    try {
        for (std::size_t tile = 0; tile < maximum_tiles && impl.cursor < extent; ++tile) {
            check_stop(stop);
            const auto count = std::min(plan.tile_width, extent - impl.cursor);
            CoarseOperationCounts completed;
            if (impl.bulk) {
                completed = impl.score_bulk(impl.cursor, count, stop);
            } else if (lag) {
                const auto row_width = mul(plan.frequency_points, arrival_peak_slots(plan.arrival_points));
                completed = score_lag_tile(impl.geometry, std::span(impl.bank).subspan(impl.cursor, count),
                    impl.observation, impl.kernels, plan.frequency_points, plan.correlation_fft_rows,
                    std::span(impl.peaks).subspan(mul(impl.cursor, row_width), mul(count, row_width)),
                    impl.variance, *impl.fft, plan.workers, stop, impl.executor, impl.slot.index);
            } else if (impl.float_fft) {
                completed = score_frequency_tile(impl.geometry, impl.bank, impl.observation, impl.grid.values,
                    impl.peaks, impl.cursor, count, impl.variance, *impl.float_fft, plan.workers, stop, impl.executor,
                    impl.slot.index, plan.observation_scale_exponent,
                    impl.q15_products);
            } else {
                completed = score_frequency_tile(impl.geometry, impl.bank, impl.observation, impl.grid.values,
                    impl.peaks, impl.cursor, count, impl.variance, *impl.fft, plan.workers, stop, impl.executor,
                    impl.slot.index);
            }
            combine(receipt.search.operations, completed);
            impl.cursor += count;
            ++receipt.completed_tiles;
        }
        receipt.state = impl.cursor == extent ? CoarseState::searched : CoarseState::searching;
    } catch (...) {
        receipt.search.wall_seconds += elapsed(began);
        receipt.state = stop.stop_requested() ? CoarseState::cancelled : CoarseState::failed;
        throw;
    }
    receipt.search.wall_seconds += elapsed(began);
}

std::optional<CoarseResult> CoarseSession::finish() {
    auto& impl = *impl_;
    auto& receipt = impl.receipt;
    if (impl.state() == CoarseState::finished) return impl.result;
    if (impl.state() != CoarseState::searched) return {};
    const auto stop = impl.cancellation.get_token();
    const auto began = PhaseClock::now();
    try {
        const auto& plan = receipt.plan;
        if (impl.bulk) {
            receipt.bulk = impl.bulk->receipt();
            require_coarse(receipt.bulk->executed && receipt.bulk->score_rows == impl.bulk_completed.score_rows,
                           "coarse bulk completed without a matching backend execution receipt");
            // All device leaves have completed. Release their lease before
            // proposal selection, which may require other executor lanes.
            impl.bulk.reset();
        }
        CoarseResult result;
        result.projected_sample_rate_hz = plan.projected_sample_rate_hz;
        result.maximum_frequency_step_hz = plan.maximum_frequency_step_hz;
        result.clock_gap_ppm = plan.clock_gap_ppm;
        result.workspace_peak_bytes = plan.workspace_reserved_bytes;
        result.frequency_points = plan.frequency_points;
        result.arrival_points = plan.arrival_points;
        result.workspaces = plan.workspaces;
        result.lag_frequency_search = plan.route == CoarseRoute::lag_groups;
        retain_proposals(result, impl.geometry, impl.slot, impl.bank, impl.peaks, impl.grid.values,
                         plan.workers, stop, impl.executor);
        refine(result, impl.geometry, impl.config, impl.slot);
        check_stop(stop);
        impl.result.emplace(std::move(result));
        impl.refresh_retained_bytes();
        check_stop(stop);
        receipt.selection.operations.selection_peak_rows = impl.peaks.size();
        receipt.proposals = impl.result->proposal_count;
        receipt.refined_coordinates = impl.result->coordinates.size();
        receipt.selection.wall_seconds = elapsed(began);
        receipt.state = CoarseState::finished;
        impl.result->work = receipt;
    } catch (...) {
        impl.result.reset();
        receipt.selection.wall_seconds = elapsed(began);
        if (stop.stop_requested()) {
            receipt.state = CoarseState::cancelled;
            return {};
        }
        receipt.state = CoarseState::failed;
        throw;
    }
    return impl.result;
}

CoarseResult score_coarse(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                          std::span<const Complex> iq, std::span<const FrequencyInterval> intervals,
                          unsigned workers, std::size_t scratch_limit_bytes, std::stop_token stop,
                          ReceiverExecutor* executor) {
    CoarseSession session(config, slot, key, iq, intervals, workers, scratch_limit_bytes, stop, executor);
    for (auto state = session.state(); state != CoarseState::searched; state = session.state()) {
        if (state == CoarseState::cancelled || state == CoarseState::failed)
            throw Error("coarse receiver stopped before completing its public grid");
        session.advance(64);
    }
    auto result = session.finish();
    if (!result) throw Error("coarse receiver cancelled before full selection");
    return std::move(*result);
}

} // namespace datapump::triad::v3::receiver_detail
