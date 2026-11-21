#include "triad_opencl_coarse_bulk.hpp"

#include "datapump/triad_receiver.hpp"
#include "triad_opencl_nvfp4_coarse.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace datapump::triad::v3::receiver_detail {
namespace {

// A block scores 512 arrivals. Narrower public windows leave most of each
// Tensor Core tile unused and keep their existing route.
constexpr std::size_t minimum_arrivals = 16;
constexpr std::size_t default_tile_frequencies = 32;
// FP4 scores choose which arrivals are rescored, and their error grows as a
// bank's two rays approach collinearity. The canonical reduced-precision
// conditioning guard applies before they select anything. A one-ray Gram is
// a scalar, whose inverse never amplifies that error.
constexpr double minimum_conditioning = 1. / 16;
constexpr std::size_t maximum_chip_bytes = std::size_t{64} << 20;
// Dense real products of one workspace and frequency, 4 x rays x arrivals x
// template rows, that an automatic one-ray session may issue: those of the
// matched two-ray receiver search on a full RTX PRO 6000, 8 x 3,377 x
// 44,971, where it took a twentieth or less of the FFT session's coarse
// seconds. Both routes scale with workspaces and frequencies, and dense work
// grows faster in arrivals x rows. Neither route is measured beyond it for
// one ray.
constexpr double maximum_one_ray_products = 8. * 3377 * 44971;
// Reasons and precision labels name the template source and the rescoring
// accumulation: every product in FP64, as before FP32 partial sums existed,
// or FP32 partial sums of 64 rows added in FP64.
constexpr std::string_view dense_reason = "opencl-nvfp4-dense-fp32-rescored";
constexpr std::string_view synthesized_reason = "opencl-nvfp4-dense-device-templates-fp32-rescored";
constexpr std::string_view dense_partial_reason = "opencl-nvfp4-dense-fp32-partial-rescored";
constexpr std::string_view synthesized_partial_reason = "opencl-nvfp4-dense-device-templates-fp32-partial-rescored";

void count(std::uint64_t& destination, std::uint64_t increment) {
    if (std::numeric_limits<std::uint64_t>::max() - destination < increment)
        throw Error("Robust OpenCL coarse receipt counter overflow");
    destination += increment;
}

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

// Fallback reasons index opencl_coarse_fallback_reasons. A name that is not
// listed there does not compile.
constexpr std::size_t fallback_reason(std::string_view name) {
    for (std::size_t i = 0; i < opencl_coarse_fallback_reasons.size(); ++i)
        if (opencl_coarse_fallback_reasons[i] == name) return i;
    throw Error("unknown Robust OpenCL coarse fallback reason");
}
constexpr auto device_templates_off = fallback_reason("device-templates-off");
constexpr auto few_arrivals = fallback_reason("few-arrivals");
constexpr auto one_ray_unqualified = fallback_reason("one-ray-unqualified");
constexpr auto dense_cost = fallback_reason("dense-cost");
constexpr auto keyed_chip_limit = fallback_reason("keyed-chip-limit");
constexpr auto kernel_extents = fallback_reason("kernel-extent");
constexpr auto workspace_lease = fallback_reason("workspace-lease");
constexpr auto host_allocation = fallback_reason("host-allocation");
constexpr auto device_allocation = fallback_reason("device-allocation");
constexpr auto ill_conditioned = fallback_reason("ill-conditioned-template");
constexpr auto unoffered = fallback_reason("unoffered");
constexpr auto lag_groups = fallback_reason("lag-group-route");
static_assert(lag_groups == static_cast<std::size_t>(CoarseSynthesisWithheld::lag_group_route) &&
              fallback_reason("mapped-profile") == static_cast<std::size_t>(CoarseSynthesisWithheld::mapped_profile) &&
              fallback_reason("midpoint-gain") == static_cast<std::size_t>(CoarseSynthesisWithheld::midpoint_gain) &&
              fallback_reason("varying-passband") == static_cast<std::size_t>(CoarseSynthesisWithheld::varying_passband) &&
              fallback_reason("fine-cell-limit") == static_cast<std::size_t>(CoarseSynthesisWithheld::fine_cell_limit) &&
              fallback_reason("description-storage") ==
                  static_cast<std::size_t>(CoarseSynthesisWithheld::description_storage));

// The smaller over the larger eigenvalue of one workspace's Gram, as the
// receiver derives it from the same entries. A one-ray Gram has ratio one.
double conditioning(const CoarseTemplateGram& gram, std::size_t taps) {
    if (taps == 1) return std::isfinite(gram.a) && gram.a > 0 ? 1 : 0;
    const auto largest = (gram.a + gram.d + std::hypot(gram.a - gram.d, 2 * std::abs(gram.b))) / 2;
    if (!(std::isfinite(largest) && largest > 0)) return 0;
    return std::max(0., (gram.a * gram.d - std::norm(gram.b)) / largest) / largest;
}

// A shape the dense search can hold, with its whole device lease.
struct Admission {
    OpenClCoarseShape shape;
    std::size_t device_bytes = 0;
};

std::optional<Admission> public_shape(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                      const CoarseTemplateSource* source, std::size_t& decline) {
    OpenClCoarseShape shape{geometry.taps, geometry.kernel_rows, geometry.lag_count, workspaces,
        geometry.frequency_tile_width ? std::min(geometry.frequency_tile_width, geometry.frequency_count)
                                      : std::min(default_tile_frequencies, geometry.frequency_count)};
    if (source) {
        if (source->chips.size_bytes() > maximum_chip_bytes) {
            decline = keyed_chip_limit;
            return {};
        }
        shape.synthesis_raw_rows = source->raw_rows;
        shape.synthesis_source_rows = source->source_rows;
        shape.synthesis_lanes = source->lanes;
        shape.synthesis_cells = source->cells;
        shape.synthesis_taps = source->taps.size();
        shape.synthesis_decimation = source->decimation;
    }
    if (OpenClNvfp4Coarse::supports(shape)) {
        // The kernels index the shape's buffers in 32 bits.
        try { return Admission{shape, OpenClNvfp4Coarse::workspace(shape).device_bytes}; }
        catch (const Error&) {}
    }
    decline = kernel_extents;
    return {};
}

const char* required_failure(std::size_t decline) {
    if (decline == workspace_lease || decline == device_allocation)
        return "OpenCL NVFP4 coarse search exceeds its device workspace";
    if (decline == ill_conditioned) return "OpenCL NVFP4 coarse search requires a better conditioned template bank";
    return "OpenCL NVFP4 coarse search has no route for this public geometry";
}

class OpenClCoarseBulk final : public CoarseBulk {
public:
    OpenClCoarseBulk(std::shared_ptr<OpenClCoarseHost> host, std::shared_ptr<const OpenClNvfp4Device> device,
                     std::unique_ptr<CoarseBulk> fallback, const OpenClCoarseOptions& options)
        : host_(std::move(host)), device_(std::move(device)), fallback_(std::move(fallback)), options_(options) {
        require(host_ && device_ && (fallback_ || options_.required), "invalid Robust OpenCL coarse session");
        reason_ = partial() ? dense_partial_reason : dense_reason;
    }

    ~OpenClCoarseBulk() override {
        coarse_.reset();
        if (lease_) host_->release(lease_);
    }

    std::string_view precision_reason() const noexcept override {
        return delegated_ ? fallback_->precision_reason() : reason_;
    }
    bool supports_fir() const noexcept override { return false; }
    // The changing-channel segmented search is a separate stage from the
    // dense frequency search. The FFT session scores it on the same device
    // whichever session later scores the frequencies.
    void plan_segmented(std::span<const CoarseSegmentedShape> shapes) override {
        if (fallback_) fallback_->plan_segmented(shapes);
    }
    void begin_segmented(std::stop_token stop) override {
        if (fallback_) fallback_->begin_segmented(stop);
    }
    void end_segmented() noexcept override {
        if (fallback_) fallback_->end_segmented();
    }
    bool segmented_scores(const CoarseSegmentedShape& shape, std::span<const std::complex<float>> spectra,
                          std::span<const Complex> column, double noise_power, std::span<double> fixed,
                          std::span<double> smooth, CoarseBulkWork& work, std::stop_token stop) override {
        return fallback_ && fallback_->segmented_scores(shape, spectra, column, noise_power, fixed, smooth, work, stop);
    }
    std::shared_ptr<const CoarseBulkFilter> prepare_filter(std::span<const double>, std::size_t,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("OpenCL coarse search requires canonical native FIR channelization");
    }
    std::vector<Complex> project_fir(const CoarseBulkFilter&, const CoarseFirCrop&, const CoarseFirSource&,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("OpenCL coarse search requires canonical native FIR channelization");
    }

    // A declined query records its reason for the session's fallback count.
    bool supports_template_synthesis(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                     const CoarseTemplateSource& source) const noexcept override {
        if (started_) return false;
        if (!options_.synthesize) {
            decline_ = device_templates_off;
            return false;
        }
        try {
            const auto admitted = eligible(geometry, workspaces, &source, decline_);
            if (admitted && admitted->device_bytes > host_->workspace_limit()) decline_ = workspace_lease;
            return admitted && admitted->device_bytes <= host_->workspace_limit();
        } catch (const std::bad_alloc&) {
            decline_ = host_allocation;
        } catch (...) {
            // Geometry and receipt errors; nothing else is expected here.
            decline_ = kernel_extents;
        }
        return false;
    }

    void withhold_template_synthesis(CoarseSynthesisWithheld reason) noexcept override {
        decline_ = static_cast<std::size_t>(reason);
    }

    bool synthesize_templates(const CoarseBulkGeometry& geometry, const CoarseTemplateSource& source,
        std::span<CoarseTemplateGram> grams, CoarseBulkWork& work, std::stop_token stop) override {
        if (started_ || !options_.synthesize) return false;
        const auto admitted = eligible(geometry, grams.size(), &source, decline_);
        if (!admitted || !open(*admitted, stop)) return false;
        started_ = true;
        const auto began = Clock::now();
        OpenClCoarseWork device;
        coarse_->synthesize_bank(source, grams, device, stop);
        if (std::ranges::any_of(grams, [&](const auto& gram) {
                return conditioning(gram, geometry.taps) < minimum_conditioning; })) {
            // Decline before an FP4 score selects anything. The transfers
            // and dispatches happened, and no column stays retained.
            coarse_.reset();
            host_->release(std::exchange(lease_, 0));
            started_ = false;
            decline_ = ill_conditioned;
            account(device, {}, began);
            return false;
        }
        BulkReceipt delta;
        delta.opencl_coarse_synthesized_columns = geometry.taps * grams.size();
        account(device, delta, began);
        count(work.fir_input_rows, device.warped_samples);
        count(work.fir_output_contributions, device.filtered_rows);
        synthesized_ = true;
        reason_ = partial() ? synthesized_partial_reason : synthesized_reason;
        return true;
    }

    void prepare(const CoarseBulkGeometry& geometry, std::span<const Complex> observation,
                 std::span<const CoarseBulkWorkspace> bank, std::span<const CoarseZoomGroup> groups,
                 CoarseBulkWork& work, std::stop_token stop) override {
        require(!prepared_ && !delegated_, "Robust OpenCL coarse session was already prepared");
        const bool resident = synthesized_;
        if (resident) {
            require(groups.empty() && bank.size() == shape_.workspaces && geometry.kernel_rows == shape_.kernel_rows &&
                    geometry.lag_count == shape_.arrivals && geometry.taps == shape_.taps &&
                    std::ranges::all_of(bank, [](const auto& row) { return row.columns[0].empty() && row.columns[1].empty(); }),
                    "Robust OpenCL coarse preparation differs from its synthesized bank");
        } else {
            // A session that scores no uploaded bank keeps the reason that
            // its synthesis was withheld or declined.
            std::optional<Admission> admitted;
            if (!groups.empty()) decline_ = lag_groups;
            else if (options_.uploaded_banks) {
                if (geometry.minimum_eigenvalue_ratio < minimum_conditioning) decline_ = ill_conditioned;
                else admitted = eligible(geometry, bank.size(), nullptr, decline_);
            }
            if (!admitted || !open(*admitted, stop)) {
                if (options_.required) throw ReceiverWorkspaceError(required_failure(decline_));
                started_ = delegated_ = true;
                BulkReceipt delta;
                delta.opencl_coarse_frequency_fallbacks = 1;
                delta.opencl_coarse_fallback_counts[decline_] = 1;
                host_->account(delta);
                fallback_->prepare(geometry, observation, bank, groups, work, stop);
                return;
            }
            started_ = true;
        }
        const auto began = Clock::now();
        OpenClCoarseWork device;
        BulkReceipt delta;
        if (!resident) {
            std::vector<std::span<const Complex>> columns;
            columns.reserve(bank.size() * geometry.taps);
            for (const auto& row : bank)
                for (std::size_t tap = 0; tap < geometry.taps; ++tap) columns.push_back(row.columns[tap]);
            coarse_->load_bank(columns, device, stop);
            delta.opencl_coarse_uploaded_columns = columns.size();
            reason_ = partial() ? dense_partial_reason : dense_reason;
        }
        std::vector<std::array<double, 4>> inverse(bank.size());
        for (std::size_t workspace = 0; workspace < bank.size(); ++workspace) {
            const auto& row = bank[workspace];
            inverse[workspace] = {row.inverse_a, row.inverse_d, row.inverse_b.real(), row.inverse_b.imag()};
        }
        coarse_->prepare(observation, inverse, geometry.projected_sample_rate_hz, geometry.variance, device, stop);
        delta.opencl_coarse_sessions = 1;
        account(device, delta, began);
        prepared_ = true;
    }

    void frequency_tile(std::span<const double> frequencies, const CoarseScoreSink& sink,
                        CoarseBulkWork& work, std::stop_token stop) override {
        if (delegated_) return fallback_->frequency_tile(frequencies, sink, work, stop);
        throw Error("OpenCL NVFP4 coarse scores stay on the device and publish arrival frontiers");
    }

    bool frequency_peaks(std::span<const double> frequencies, std::size_t separation,
                         const CoarsePeakSink& sink, CoarseBulkWork& work, std::stop_token stop) override {
        if (delegated_) return fallback_->frequency_peaks(frequencies, separation, sink, work, stop);
        require(prepared_ && static_cast<bool>(sink), "Robust OpenCL coarse session is not prepared");
        const auto began = Clock::now();
        frontier_.resize(shape_.workspaces * frequencies.size());
        OpenClCoarseWork device;
        coarse_->peaks(frequencies, separation, frontier_, device, stop);
        BulkReceipt delta;
        delta.score_rows = device.score_rows;
        delta.executed = true;
        account(device, delta, began);
        count(work.score_rows, device.score_rows);
        count(work.workspace_frequency_pairs, frontier_.size());
        sink(0, shape_.workspaces, frontier_);
        return true;
    }

    void lag_tile(std::size_t first, std::size_t rows, const CoarseScoreSink& sink, CoarseBulkWork& work,
                  std::stop_token stop) override {
        if (delegated_) return fallback_->lag_tile(first, rows, sink, work, stop);
        throw Error("OpenCL NVFP4 coarse search has no lag-group route");
    }

    BulkReceipt receipt() const override {
        if (delegated_) return fallback_->receipt();
        auto result = host_->receipt();
        // The session receipt carries its own work beside the backend identity.
        result.upload_bytes = local_.upload_bytes;
        result.download_bytes = local_.download_bytes;
        result.synchronizations = local_.synchronizations;
        result.score_rows = local_.score_rows;
        result.wall_seconds = local_.wall_seconds;
        result.executed = local_.executed;
        result.opencl_coarse_score_rows = result.opencl_coarse_rescored_peaks = 0;
        result.opencl_coarse_issued_products = result.opencl_coarse_useful_products = 0;
        result.opencl_coarse_synthesized_columns = result.opencl_coarse_uploaded_columns = 0;
        result.opencl_coarse_upload_bytes = result.opencl_coarse_download_bytes = 0;
        result.opencl_coarse_dispatches = result.opencl_coarse_blocking_transfers = 0;
        result.opencl_coarse_sessions = result.opencl_coarse_frequency_fallbacks = 0;
        result.opencl_coarse_fallback_counts = {};
        result.opencl_coarse_rescore_fp32_partial_peaks = result.opencl_coarse_rescore_fp64_peaks = 0;
        result.opencl_coarse_product_seconds = result.opencl_coarse_packing_seconds = 0;
        result.opencl_coarse_selection_seconds = result.opencl_coarse_synthesis_seconds = 0;
        result.opencl_coarse_render_seconds = result.opencl_coarse_filter_seconds = 0;
        result.opencl_coarse_gram_seconds = result.opencl_coarse_rescore_seconds = 0;
        add_opencl_coarse_work(result, local_);
        if (fallback_) {
            // The FFT session scored only the segmented timing search here.
            // Its own work joins this session's, not the backend totals.
            const auto segmented = fallback_->receipt();
            result.upload_bytes += segmented.upload_bytes;
            result.download_bytes += segmented.download_bytes;
            result.synchronizations += segmented.synchronizations;
            result.wall_seconds += segmented.wall_seconds;
            result.executed = result.executed || segmented.vulkan_segmented_sessions;
            result.fft_calls = segmented.fft_calls;
            result.fft_rows = segmented.fft_rows;
            result.vulkan_dispatches = segmented.vulkan_dispatches;
            result.vulkan_timed_submissions = segmented.vulkan_timed_submissions;
            result.vulkan_device_command_seconds = segmented.vulkan_device_command_seconds;
            // Its plan, cache and shader figures are session-local too.
            result.vulkan_fft_cache_hits = segmented.vulkan_fft_cache_hits;
            result.vulkan_fft_cache_misses = segmented.vulkan_fft_cache_misses;
            result.vulkan_fft_plan_seconds = segmented.vulkan_fft_plan_seconds;
            result.vulkan_shader_compile_seconds = segmented.vulkan_shader_compile_seconds;
            result.vulkan_shader_spirv_compiles = segmented.vulkan_shader_spirv_compiles;
            result.vulkan_shader_spirv_cache_hits = segmented.vulkan_shader_spirv_cache_hits;
            result.fft_plan_cache_count = segmented.fft_plan_cache_count;
            result.vulkan_segmented_sessions = segmented.vulkan_segmented_sessions;
            result.vulkan_segmented_forward_ffts = segmented.vulkan_segmented_forward_ffts;
            result.vulkan_segmented_inverse_ffts = segmented.vulkan_segmented_inverse_ffts;
            result.vulkan_segmented_score_rows = segmented.vulkan_segmented_score_rows;
            result.vulkan_segmented_planned_shapes = segmented.vulkan_segmented_planned_shapes;
            result.vulkan_segmented_declined_calls = segmented.vulkan_segmented_declined_calls;
            result.vulkan_segmented_decline_reason = segmented.vulkan_segmented_decline_reason;
            result.vulkan_segmented_native_fallbacks = segmented.vulkan_segmented_native_fallbacks;
            result.vulkan_segmented_fallback_reason = segmented.vulkan_segmented_fallback_reason;
        }
        result.workspace_limit_bytes = lease_;
        result.workspace_scope = "per_coarse_session";
        result.precision = synthesized_
            ? (partial() ? "nvfp4_dense_coarse_device_templates_fp32_partial_rescored_peaks"
                         : "nvfp4_dense_coarse_device_templates_fp32_rescored_peaks")
            : (partial() ? "nvfp4_dense_coarse_fp32_partial_rescored_peaks" : "nvfp4_dense_coarse_fp32_rescored_peaks");
        return result;
    }

    CoarseBulkCompletedWork completed_work() const override {
        if (delegated_) return fallback_->completed_work();
        return {local_.score_rows, local_.executed};
    }

private:
    bool partial() const noexcept { return options_.rescore == OpenClRescoreArithmetic::fp32_partial; }

    using Clock = std::chrono::steady_clock;

    // One or two rays of a frequency-tile session, each reason recorded. An
    // automatic route admits one ray only when qualified, and within the
    // measured dense cost.
    std::optional<Admission> eligible(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                      const CoarseTemplateSource* source, std::size_t& decline) const {
        if ((geometry.taps != 1 && geometry.taps != 2) || !workspaces || !geometry.kernel_rows ||
            !geometry.frequency_count) {
            decline = kernel_extents;
            return {};
        }
        if (!options_.required && geometry.lag_count < minimum_arrivals) {
            decline = few_arrivals;
            return {};
        }
        if (!options_.required && geometry.taps == 1) {
            if (!options_.one_ray) {
                decline = one_ray_unqualified;
                return {};
            }
            if (4. * static_cast<double>(geometry.lag_count) * static_cast<double>(geometry.kernel_rows) >
                maximum_one_ray_products) {
                decline = dense_cost;
                return {};
            }
        }
        return public_shape(geometry, workspaces, source, decline);
    }

    // Leases the whole session before allocating. False leaves nothing held
    // and records why.
    bool open(const Admission& admitted, std::stop_token stop) {
        const auto bytes = admitted.device_bytes;
        if (bytes > host_->workspace_limit()) {
            decline_ = workspace_lease;
            return false;
        }
        host_->acquire(bytes, stop);
        try {
            coarse_ = device_->make_coarse(admitted.shape, bytes, options_.profile, options_.rescore);
        } catch (...) {
            host_->release(bytes);
            throw;
        }
        if (!coarse_) {
            host_->release(bytes);
            decline_ = device_allocation;
            return false;
        }
        lease_ = bytes;
        shape_ = admitted.shape;
        return true;
    }

    void account(const OpenClCoarseWork& device, BulkReceipt delta, Clock::time_point began) {
        delta.upload_bytes = device.upload_bytes;
        delta.download_bytes = device.download_bytes;
        delta.synchronizations = device.blocking_transfers;
        delta.wall_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        delta.opencl_coarse_score_rows = device.score_rows;
        delta.opencl_coarse_rescored_peaks = device.rescored_peaks;
        (options_.rescore == OpenClRescoreArithmetic::fp32_partial ? delta.opencl_coarse_rescore_fp32_partial_peaks
                                                                   : delta.opencl_coarse_rescore_fp64_peaks) =
            device.rescored_peaks;
        delta.opencl_coarse_issued_products = device.issued_products;
        delta.opencl_coarse_useful_products = device.useful_products;
        delta.opencl_coarse_upload_bytes = device.upload_bytes;
        delta.opencl_coarse_download_bytes = device.download_bytes;
        delta.opencl_coarse_dispatches = device.dispatches;
        delta.opencl_coarse_blocking_transfers = device.blocking_transfers;
        delta.opencl_coarse_product_seconds = device.product_seconds;
        delta.opencl_coarse_packing_seconds = device.packing_seconds;
        delta.opencl_coarse_selection_seconds = device.selection_seconds;
        delta.opencl_coarse_synthesis_seconds = device.synthesis_seconds;
        delta.opencl_coarse_render_seconds = device.render_seconds;
        delta.opencl_coarse_filter_seconds = device.filter_seconds;
        delta.opencl_coarse_gram_seconds = device.gram_seconds;
        delta.opencl_coarse_rescore_seconds = device.rescore_seconds;
        count(local_.upload_bytes, delta.upload_bytes);
        count(local_.download_bytes, delta.download_bytes);
        count(local_.synchronizations, delta.synchronizations);
        count(local_.score_rows, delta.score_rows);
        local_.wall_seconds += delta.wall_seconds;
        local_.executed |= delta.executed;
        add_opencl_coarse_work(local_, delta);
        host_->account(delta);
    }

    std::shared_ptr<OpenClCoarseHost> host_;
    std::shared_ptr<const OpenClNvfp4Device> device_;
    std::unique_ptr<CoarseBulk> fallback_;
    std::unique_ptr<OpenClNvfp4Coarse> coarse_;
    OpenClCoarseOptions options_;
    OpenClCoarseShape shape_;
    BulkReceipt local_;
    std::vector<CoarseBulkPeaks> frontier_;
    std::string_view reason_ = dense_reason;
    // The fallback reason of a session that has not scored, as an index of
    // opencl_coarse_fallback_reasons. A declined const query records it.
    mutable std::size_t decline_ = unoffered;
    std::size_t lease_ = 0;
    bool started_ = false, synthesized_ = false, prepared_ = false, delegated_ = false;
};

} // namespace

void add_opencl_coarse_work(BulkReceipt& to, const BulkReceipt& from) noexcept {
    const auto add = [](std::uint64_t& destination, std::uint64_t increment) {
        destination = increment > std::numeric_limits<std::uint64_t>::max() - destination
            ? std::numeric_limits<std::uint64_t>::max() : destination + increment;
    };
    add(to.opencl_coarse_score_rows, from.opencl_coarse_score_rows);
    add(to.opencl_coarse_rescored_peaks, from.opencl_coarse_rescored_peaks);
    add(to.opencl_coarse_rescore_fp32_partial_peaks, from.opencl_coarse_rescore_fp32_partial_peaks);
    add(to.opencl_coarse_rescore_fp64_peaks, from.opencl_coarse_rescore_fp64_peaks);
    add(to.opencl_coarse_issued_products, from.opencl_coarse_issued_products);
    add(to.opencl_coarse_useful_products, from.opencl_coarse_useful_products);
    add(to.opencl_coarse_synthesized_columns, from.opencl_coarse_synthesized_columns);
    add(to.opencl_coarse_uploaded_columns, from.opencl_coarse_uploaded_columns);
    add(to.opencl_coarse_upload_bytes, from.opencl_coarse_upload_bytes);
    add(to.opencl_coarse_download_bytes, from.opencl_coarse_download_bytes);
    add(to.opencl_coarse_dispatches, from.opencl_coarse_dispatches);
    add(to.opencl_coarse_blocking_transfers, from.opencl_coarse_blocking_transfers);
    add(to.opencl_coarse_sessions, from.opencl_coarse_sessions);
    add(to.opencl_coarse_frequency_fallbacks, from.opencl_coarse_frequency_fallbacks);
    for (std::size_t i = 0; i < to.opencl_coarse_fallback_counts.size(); ++i)
        add(to.opencl_coarse_fallback_counts[i], from.opencl_coarse_fallback_counts[i]);
    to.opencl_coarse_product_seconds += from.opencl_coarse_product_seconds;
    to.opencl_coarse_packing_seconds += from.opencl_coarse_packing_seconds;
    to.opencl_coarse_selection_seconds += from.opencl_coarse_selection_seconds;
    to.opencl_coarse_synthesis_seconds += from.opencl_coarse_synthesis_seconds;
    to.opencl_coarse_render_seconds += from.opencl_coarse_render_seconds;
    to.opencl_coarse_filter_seconds += from.opencl_coarse_filter_seconds;
    to.opencl_coarse_gram_seconds += from.opencl_coarse_gram_seconds;
    to.opencl_coarse_rescore_seconds += from.opencl_coarse_rescore_seconds;
}

std::unique_ptr<CoarseBulk> make_opencl_coarse_bulk(std::shared_ptr<OpenClCoarseHost> host,
        std::shared_ptr<const OpenClNvfp4Device> device, std::unique_ptr<CoarseBulk> fallback,
        const OpenClCoarseOptions& options) {
    return std::make_unique<OpenClCoarseBulk>(std::move(host), std::move(device), std::move(fallback), options);
}

} // namespace datapump::triad::v3::receiver_detail
