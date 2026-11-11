#pragma once

#include "datapump/types.hpp"

#include <cstddef>
#include <memory>

namespace datapump::triad::v3 {

namespace receiver_detail { class ReceiverBulkBackend; }

enum class TensorDevice { cpu, cuda };
enum class TensorPrecision { automatic, fp64, fp32_minimum };
// Optional post-channelization coarse contractions and half FFT arithmetic.
// The full FP32/FP64 FFT search remains the default and startup fallback.
enum class TensorCoarseMath { fft, fp16, int8, fp16_fft };

class TensorBackendUnavailable : public Error {
public:
    using Error::Error;
};

struct TensorReceiverOptions {
    TensorDevice device = TensorDevice::cuda;
    unsigned device_index = 0;
    // One workspace for resident and computational tensors across sessions.
    // The native receiver separately reserves host transfer staging.
    std::size_t workspace_bytes = 0;
    TensorPrecision precision = TensorPrecision::automatic;
    TensorCoarseMath coarse_math = TensorCoarseMath::fft;
};

// Defined by the optional compiled backend target. A request for CUDA
// requires an available CUDA device and never constructs a CPU backend.
std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_tensor_receiver_backend(const TensorReceiverOptions&);

// Native application selection: choose an available CUDA GPU and a shared
// workspace from its usable memory. Explicit probe options remain exact.
std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_automatic_cuda_receiver_backend(TensorPrecision precision = TensorPrecision::automatic);

class VulkanBackendUnavailable : public Error {
public:
    using Error::Error;
};

enum class VulkanCoarsePrecision { automatic, fp32, fp16_storage, direct_fp32, direct_fp16_storage, direct_fp16 };

struct VulkanReceiverOptions {
    unsigned device_index = 0;
    // Per-coarse-owner allocation ceiling. On integrated GPUs this storage
    // consumes host RAM and is included in the native DSP preflight.
    std::size_t workspace_bytes = 0;
    // Automatic selects reduced precision on qualified hardware and geometry.
    VulkanCoarsePrecision coarse_precision = VulkanCoarsePrecision::automatic;
    // Applications can permit a qualified native chirp-Z route before
    // device scoring. Explicit device probes retain the default.
    bool prefer_native_exact_chirp_z = false;
    // Exercise the portable phase/scoring route even on a device with shader
    // FP64. Otherwise selected automatically when FP64/INT64 is unavailable.
    bool force_host_scoring = false;
};

// Optional native Vulkan broad-frequency search. Channelization, fine state,
// lag-group search and the compact tight-clock FP64 class remain native CPU.
// A requested device must be a physical GPU; software Vulkan is rejected.
std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_vulkan_receiver_backend(const VulkanReceiverOptions&);

// Select a compatible physical GPU, rather than assuming enumeration index 0.
std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_automatic_vulkan_receiver_backend(const VulkanReceiverOptions&);

class GpuBackendUnavailable : public Error {
public:
    using Error::Error;
};

struct GpuReceiverOptions {
    bool fp32_minimum = false;
};

// Shared GUI and probe entry point. Prefer native CUDA where compiled and
// available, otherwise a compatible physical Vulkan GPU. Never selects CPU
// emulation; individual stages retain their explicit native routing contracts.
bool gpu_receiver_backend_available() noexcept;
std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_gpu_receiver_backend(const GpuReceiverOptions& = {});

} // namespace datapump::triad::v3
