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
    // Explicit probe qualification of the changing-channel FP32 FFT scorer.
    bool segmented_timing = false;
    // Explicit probe mode for qualifying the CUDA fine template projection.
    bool qualify_nvfp4_projection = false;
    bool qualify_cufft_projection = false;
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

// opencl_nvfp4 requires the dense FP4 Tensor Core frequency search of the
// Blackwell OpenCL module and reports geometry it cannot admit.
enum class VulkanCoarsePrecision {
    automatic, fp32, fp16_storage, direct_fp32, direct_fp16_storage, direct_fp16, opencl_nvfp4
};
enum class VulkanFineProjection {
    automatic, native, opencl_nvfp4, opencl_resident_nvfp4, opencl_resident_fft
};

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
    VulkanFineProjection fine_projection = VulkanFineProjection::automatic;
    // Record OpenCL device command intervals in the receipt. Developer
    // probes use this to separate Tensor Core time from complete service.
    bool profile_opencl = false;
    // Also time the resident fine session's host spans and commands. It reads
    // the clock around every staged column, so timing runs leave it off.
    bool profile_opencl_fine = false;
    // Let the OpenCL coarse search render its template bank from the keyed
    // coefficients. Off keeps native columns for a matched comparison.
    bool opencl_device_templates = true;
    // Let resident OpenCL fine batches render their dense source tiles from
    // the keyed coefficients. Off uploads native tiles, as a matched control.
    bool opencl_fine_device_tiles = true;
    // Public blocks of one resident fine batch, an even number from 2 to 64;
    // an odd value runs one block fewer. Zero selects the default window.
    // Two keeps one dispatch per batch.
    std::size_t opencl_fine_window_blocks = 0;
    // With device tiles, submit the mappings of every this many blocks of a
    // resident fine batch while staging the next ones, up to 64. Zero stages
    // a whole batch before the device starts, as a matched control.
    std::size_t opencl_fine_stream_blocks = 4;
};

// Optional Vulkan broad-frequency search and Blackwell OpenCL fine projection.
// Channelization, fine state, lag groups and compact tight-clock FP64 stay native.
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
