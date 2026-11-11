#include "datapump/triad_acceleration.hpp"
#include "triad_receiver_bulk.hpp"

#include <utility>

namespace datapump::triad::v3 {
namespace {

// Preserve backend ownership and stage policies while exposing the application
// selection separately from numerical fallback and actual completed work.
class SelectedGpu final : public receiver_detail::ReceiverBulkBackend {
public:
    SelectedGpu(std::shared_ptr<receiver_detail::ReceiverBulkBackend> backend, std::string reason)
        : backend_(std::move(backend)), reason_(std::move(reason)) {}
    bool supports_fine() const noexcept override { return backend_->supports_fine(); }
    std::string_view fine_fallback_reason(const receiver_detail::FineBulkGeometry& geometry) const noexcept override {
        return backend_->fine_fallback_reason(geometry);
    }
    bool permits_native_exact_chirp_z() const noexcept override { return backend_->permits_native_exact_chirp_z(); }
    bool supports_exact_device_chirp_z() const noexcept override { return backend_->supports_exact_device_chirp_z(); }
    std::size_t maximum_frequency_tile_width(const receiver_detail::CoarseBulkGeometry& geometry,
            std::size_t workspaces, std::size_t requested_width, double clock) const override {
        return backend_->maximum_frequency_tile_width(geometry, workspaces, requested_width, clock);
    }
    std::optional<receiver_detail::CoarseFrequencyPlan> frequency_plan(
            const receiver_detail::CoarseBulkGeometry& geometry, std::size_t workspaces,
            std::size_t maximum_fft_rows) const override {
        return backend_->frequency_plan(geometry, workspaces, maximum_fft_rows);
    }
    std::string_view coarse_fallback_reason(double rate, double clock,
            receiver_detail::CoarseRoute route) const noexcept override {
        return backend_->coarse_fallback_reason(rate, clock, route);
    }
    void record_native_coarse(std::string_view reason) override { backend_->record_native_coarse(reason); }
    void record_native_fine(std::string_view reason) override { backend_->record_native_fine(reason); }
    std::unique_ptr<receiver_detail::FineBulk> make_fine(double rate, double clock) override {
        return backend_->make_fine(rate, clock);
    }
    std::unique_ptr<receiver_detail::CoarseBulk> make_coarse(double rate, double clock) override {
        return backend_->make_coarse(rate, clock);
    }
    std::size_t persistent_host_workspace() const noexcept override { return backend_->persistent_host_workspace(); }
    std::size_t fine_host_bytes(std::size_t rows, std::size_t taps) const override {
        return backend_->fine_host_bytes(rows, taps);
    }
    std::size_t coarse_host_bytes(std::size_t rows, std::size_t frequencies, std::size_t arrivals) const override {
        return backend_->coarse_host_bytes(rows, frequencies, arrivals);
    }
    receiver_detail::BulkReceipt receipt() const override { return annotate(backend_->receipt()); }
    receiver_detail::BulkReceipt status_receipt() const override { return annotate(backend_->status_receipt()); }
private:
    receiver_detail::BulkReceipt annotate(receiver_detail::BulkReceipt receipt) const {
        receipt.selection_reason = reason_ + (receipt.selection_reason.empty() ? "" : "; " + receipt.selection_reason);
        return receipt;
    }
    std::shared_ptr<receiver_detail::ReceiverBulkBackend> backend_;
    std::string reason_;
};
}

bool gpu_receiver_backend_available() noexcept {
#if defined(DATAPUMP_HAVE_TRIAD_CUDA) || defined(DATAPUMP_HAVE_TRIAD_VULKAN)
    return true;
#else
    return false;
#endif
}

std::shared_ptr<receiver_detail::ReceiverBulkBackend> make_gpu_receiver_backend(const GpuReceiverOptions& options) {
    std::string unavailable;
#if defined(DATAPUMP_HAVE_TRIAD_CUDA)
    try {
        const auto precision = options.fp32_minimum ? TensorPrecision::fp32_minimum : TensorPrecision::automatic;
        return std::make_shared<SelectedGpu>(make_automatic_cuda_receiver_backend(precision),
            "automatic CUDA receiver");
    } catch (const TensorBackendUnavailable& error) {
        unavailable = std::string(error.what()).substr(0, 512);
    }
#endif
#if defined(DATAPUMP_HAVE_TRIAD_VULKAN)
    try {
        VulkanReceiverOptions vulkan;
        vulkan.coarse_precision = options.fp32_minimum ? VulkanCoarsePrecision::fp32 : VulkanCoarsePrecision::automatic;
        vulkan.prefer_native_exact_chirp_z = !options.fp32_minimum;
        auto backend = make_automatic_vulkan_receiver_backend(vulkan);
        return std::make_shared<SelectedGpu>(std::move(backend),
            unavailable.empty() ? "automatic Vulkan receiver: GPU search with native fine processing" :
                "automatic Vulkan receiver; CUDA unavailable: " + unavailable);
    } catch (const VulkanBackendUnavailable& error) {
        if (!unavailable.empty()) unavailable += "; ";
        unavailable += std::string(error.what()).substr(0, 512);
    }
#else
    (void)options;
#endif
    if (unavailable.empty()) unavailable = "this build has no native CUDA or Vulkan receiver backend";
    throw GpuBackendUnavailable("GPU reception unavailable: " + unavailable);
}
} // namespace datapump::triad::v3
