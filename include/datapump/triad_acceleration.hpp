#pragma once

#include "datapump/types.hpp"

#include <cstddef>
#include <memory>

namespace datapump::triad::v3 {

namespace receiver_detail { class ReceiverBulkBackend; }

enum class TensorDevice { cpu, cuda };
enum class TensorPrecision { automatic, fp64 };
// Optional post-channelization coarse contractions. The full FFT search is
// retained for every unsupported shape and remains the scalable default.
enum class TensorCoarseMath { fft, fp16, int8 };

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
};

// Optional native Vulkan broad-frequency search. Channelization, fine state,
// lag-group search and the compact tight-clock FP64 class remain native CPU.
// A requested device must be a physical GPU; software Vulkan is rejected.
std::shared_ptr<receiver_detail::ReceiverBulkBackend>
make_vulkan_receiver_backend(const VulkanReceiverOptions&);

} // namespace datapump::triad::v3
