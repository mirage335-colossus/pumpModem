#include "triad_opencl_coarse_bulk.hpp"

#include "datapump/triad_receiver.hpp"
#include "triad_opencl_nvfp4_coarse.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
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
// conditioning guard applies before they select anything.
constexpr double minimum_conditioning = 1. / 16;
constexpr std::size_t maximum_chip_bytes = std::size_t{64} << 20;
constexpr std::string_view dense_reason = "opencl-nvfp4-dense-fp32-rescored";
constexpr std::string_view synthesized_reason = "opencl-nvfp4-dense-device-templates-fp32-rescored";

void count(std::uint64_t& destination, std::uint64_t increment) {
    if (std::numeric_limits<std::uint64_t>::max() - destination < increment)
        throw Error("Robust OpenCL coarse receipt counter overflow");
    destination += increment;
}

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

// The smaller over the larger eigenvalue of one workspace's two-ray Gram, as
// the receiver derives it from the same entries.
double conditioning(const CoarseTemplateGram& gram) {
    const auto largest = (gram.a + gram.d + std::hypot(gram.a - gram.d, 2 * std::abs(gram.b))) / 2;
    if (!(std::isfinite(largest) && largest > 0)) return 0;
    return std::max(0., (gram.a * gram.d - std::norm(gram.b)) / largest) / largest;
}

std::optional<OpenClCoarseShape> public_shape(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                              const CoarseTemplateSource* source) {
    OpenClCoarseShape shape{geometry.taps, geometry.kernel_rows, geometry.lag_count, workspaces,
        geometry.frequency_tile_width ? std::min(geometry.frequency_tile_width, geometry.frequency_count)
                                      : std::min(default_tile_frequencies, geometry.frequency_count)};
    if (source) {
        if (source->chips.size_bytes() > maximum_chip_bytes) return {};
        shape.synthesis_raw_rows = source->raw_rows;
        shape.synthesis_source_rows = source->source_rows;
        shape.synthesis_lanes = source->lanes;
        shape.synthesis_cells = source->cells;
        shape.synthesis_taps = source->taps.size();
    }
    if (!OpenClNvfp4Coarse::supports(shape)) return {};
    return shape;
}

class OpenClCoarseBulk final : public CoarseBulk {
public:
    OpenClCoarseBulk(std::shared_ptr<OpenClCoarseHost> host, std::shared_ptr<const OpenClNvfp4Device> device,
                     std::unique_ptr<CoarseBulk> fallback, const OpenClCoarseOptions& options)
        : host_(std::move(host)), device_(std::move(device)), fallback_(std::move(fallback)), options_(options) {
        require(host_ && device_ && (fallback_ || options_.required), "invalid Robust OpenCL coarse session");
    }

    ~OpenClCoarseBulk() override {
        coarse_.reset();
        if (lease_) host_->release(lease_);
    }

    std::string_view precision_reason() const noexcept override {
        return delegated_ ? fallback_->precision_reason() : reason_;
    }
    bool supports_fir() const noexcept override { return false; }
    void plan_segmented(std::span<const CoarseSegmentedShape> shapes) override {
        if (fallback_) fallback_->plan_segmented(shapes);
    }
    std::shared_ptr<const CoarseBulkFilter> prepare_filter(std::span<const double>, std::size_t,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("OpenCL coarse search requires canonical native FIR channelization");
    }
    std::vector<Complex> project_fir(const CoarseBulkFilter&, const CoarseFirCrop&, const CoarseFirSource&,
        CoarseBulkWork&, std::stop_token) override {
        throw Error("OpenCL coarse search requires canonical native FIR channelization");
    }

    bool supports_template_synthesis(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                     const CoarseTemplateSource& source) const noexcept override {
        try {
            if (!options_.synthesize || started_) return false;
            const auto shape = eligible(geometry, workspaces, &source);
            return shape && OpenClNvfp4Coarse::workspace(*shape).device_bytes <= host_->workspace_limit();
        } catch (...) {
            return false;
        }
    }

    bool synthesize_templates(const CoarseBulkGeometry& geometry, const CoarseTemplateSource& source,
        std::span<CoarseTemplateGram> grams, CoarseBulkWork& work, std::stop_token stop) override {
        if (started_ || !options_.synthesize) return false;
        const auto shape = eligible(geometry, grams.size(), &source);
        if (!shape || !open(*shape, stop)) return false;
        started_ = true;
        const auto began = Clock::now();
        OpenClCoarseWork device;
        coarse_->synthesize_bank(source, grams, device, stop);
        if (std::ranges::any_of(grams, [](const auto& gram) { return conditioning(gram) < minimum_conditioning; })) {
            // Decline before an FP4 score selects anything. The transfers
            // and dispatches happened, and no column stays retained.
            coarse_.reset();
            host_->release(std::exchange(lease_, 0));
            started_ = false;
            account(device, {}, began);
            return false;
        }
        BulkReceipt delta;
        delta.opencl_coarse_synthesized_columns = geometry.taps * grams.size();
        account(device, delta, began);
        count(work.fir_input_rows, device.warped_samples);
        count(work.fir_output_contributions, device.filtered_rows);
        synthesized_ = true;
        reason_ = synthesized_reason;
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
            const bool conditioned = geometry.minimum_eigenvalue_ratio >= minimum_conditioning;
            const auto shape = groups.empty() && options_.uploaded_banks && conditioned
                ? eligible(geometry, bank.size(), nullptr) : std::nullopt;
            if (!shape || !open(*shape, stop)) {
                if (options_.required)
                    throw ReceiverWorkspaceError(
                        shape ? "OpenCL NVFP4 coarse search exceeds its device workspace" :
                        !conditioned ? "OpenCL NVFP4 coarse search requires a better conditioned template bank" :
                        "OpenCL NVFP4 coarse search has no route for this public geometry");
                started_ = delegated_ = true;
                BulkReceipt delta;
                delta.opencl_coarse_frequency_fallbacks = 1;
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
            reason_ = dense_reason;
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
        result.opencl_coarse_product_seconds = result.opencl_coarse_packing_seconds = 0;
        result.opencl_coarse_selection_seconds = result.opencl_coarse_synthesis_seconds = 0;
        add_opencl_coarse_work(result, local_);
        result.workspace_limit_bytes = lease_;
        result.workspace_scope = "per_coarse_session";
        result.precision = synthesized_ ? "nvfp4_dense_coarse_device_templates_fp32_rescored_peaks"
                                        : "nvfp4_dense_coarse_fp32_rescored_peaks";
        return result;
    }

    CoarseBulkCompletedWork completed_work() const override {
        if (delegated_) return fallback_->completed_work();
        return {local_.score_rows, local_.executed};
    }

private:
    using Clock = std::chrono::steady_clock;

    std::optional<OpenClCoarseShape> eligible(const CoarseBulkGeometry& geometry, std::size_t workspaces,
                                              const CoarseTemplateSource* source) const {
        if (geometry.taps != 2 || !workspaces || !geometry.kernel_rows || !geometry.frequency_count ||
            (!options_.required && geometry.lag_count < minimum_arrivals)) return {};
        return public_shape(geometry, workspaces, source);
    }

    // Leases the whole session before allocating. False leaves nothing held.
    bool open(const OpenClCoarseShape& shape, std::stop_token stop) {
        const auto bytes = OpenClNvfp4Coarse::workspace(shape).device_bytes;
        if (bytes > host_->workspace_limit()) return false;
        host_->acquire(bytes, stop);
        try {
            coarse_ = device_->make_coarse(shape, bytes, options_.profile);
        } catch (...) {
            host_->release(bytes);
            throw;
        }
        if (!coarse_) {
            host_->release(bytes);
            return false;
        }
        lease_ = bytes;
        shape_ = shape;
        return true;
    }

    void account(const OpenClCoarseWork& device, BulkReceipt delta, Clock::time_point began) {
        delta.upload_bytes = device.upload_bytes;
        delta.download_bytes = device.download_bytes;
        delta.synchronizations = device.blocking_transfers;
        delta.wall_seconds = std::chrono::duration<double>(Clock::now() - began).count();
        delta.opencl_coarse_score_rows = device.score_rows;
        delta.opencl_coarse_rescored_peaks = device.rescored_peaks;
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
    to.opencl_coarse_product_seconds += from.opencl_coarse_product_seconds;
    to.opencl_coarse_packing_seconds += from.opencl_coarse_packing_seconds;
    to.opencl_coarse_selection_seconds += from.opencl_coarse_selection_seconds;
    to.opencl_coarse_synthesis_seconds += from.opencl_coarse_synthesis_seconds;
}

std::unique_ptr<CoarseBulk> make_opencl_coarse_bulk(std::shared_ptr<OpenClCoarseHost> host,
        std::shared_ptr<const OpenClNvfp4Device> device, std::unique_ptr<CoarseBulk> fallback,
        const OpenClCoarseOptions& options) {
    return std::make_unique<OpenClCoarseBulk>(std::move(host), std::move(device), std::move(fallback), options);
}

} // namespace datapump::triad::v3::receiver_detail
