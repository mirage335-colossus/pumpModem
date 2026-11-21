#pragma once

// Vulkan fine-stage device sessions: the signed-lag gain direct rank, drift
// proposals and varying statistics (docs/triad-gpu-optimization.md,
// "Fine-stage device ports"). This header holds the pure selection policy
// and reason names, which need no device, and the entry points that
// src/triad_vulkan_{gain,drift,varying}.cpp define on the shared core.

#include "datapump/triad_acceleration.hpp"
#include "triad_receiver_bulk.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>

namespace datapump::triad::v3::receiver_detail::vulkan {

struct Shared;

// "vulkan-<family>-<suffix>" for every family and StageReason, with static
// storage for the receipts' string views.
struct StageReasonNames {
    static constexpr std::size_t capacity = 48;
    std::array<std::array<std::array<char, capacity>, stage_reason_count>, stage_families.size()> text{};
    std::array<std::array<std::size_t, stage_reason_count>, stage_families.size()> size{};
};
consteval StageReasonNames make_stage_reason_names() {
    StageReasonNames names;
    for (std::size_t family = 0; family < stage_families.size(); ++family)
        for (std::size_t reason = 0; reason < stage_reason_count; ++reason) {
            auto& text = names.text[family][reason];
            auto& size = names.size[family][reason];
            const auto append = [&](std::string_view part) {
                for (const char c : part) {
                    if (size == StageReasonNames::capacity) throw "stage reason name exceeds its capacity";
                    text[size++] = c;
                }
            };
            append("vulkan-");
            append(stage_family_reason_names[family]);
            append("-");
            append(stage_reason_suffix(static_cast<StageReason>(reason)));
        }
    return names;
}
inline constexpr StageReasonNames stage_reason_names = make_stage_reason_names();
constexpr std::string_view stage_reason_text(StageFamily family, StageReason reason) noexcept {
    const auto f = static_cast<std::size_t>(family), r = static_cast<std::size_t>(reason);
    return {stage_reason_names.text[f][r].data(), stage_reason_names.size[f][r]};
}

// The inputs of one family's selection, all public: the requested route,
// an explicit native FP64 coarse request, forced host scoring, device FP64
// and INT64 shaders, the family's shape check (true for a shape-independent
// query), automatic qualification of the family on this device class, and
// whether the family has a device implementation.
struct StagePolicy {
    VulkanStageRoute route = VulkanStageRoute::automatic;
    bool fp64_forced = false, host_scoring_forced = false, shader_fp64 = true, shape_supported = true;
    bool automatic_qualified = false, implemented = false;
};
// Pure policy, shared by preflight and execution, in this order: native
// requested, FP64 requested, host scoring forced, shader FP64/INT64
// unavailable, unsupported shape; then an explicit device route accepts,
// and automatic selection requires the family's qualification on this
// device class. An accepted stage without a device implementation declines
// as unimplemented. Empty selects the device.
constexpr std::optional<StageReason> stage_policy(const StagePolicy& policy) noexcept {
    if (policy.route == VulkanStageRoute::native) return StageReason::native_requested;
    if (policy.fp64_forced) return StageReason::fp64_requested;
    if (policy.host_scoring_forced) return StageReason::host_scoring_forced;
    if (!policy.shader_fp64) return StageReason::shader_fp64_unavailable;
    if (!policy.shape_supported) return StageReason::unsupported_shape;
    if (policy.route == VulkanStageRoute::automatic && !policy.automatic_qualified)
        return StageReason::unqualified_device;
    if (!policy.implemented) return StageReason::unimplemented;
    return std::nullopt;
}

constexpr VulkanStageRoute stage_route(const VulkanReceiverOptions& options, StageFamily family) noexcept {
    return family == StageFamily::gain_rank ? options.gain_rank
         : family == StageFamily::drift ? options.drift_proposals : options.varying_fine;
}

// Transient host memory of one session's VkFFT plan generation and shader
// compilation (one plan or shader at a time), and of a coarse session's.
inline constexpr std::size_t compiler_host_bound = std::size_t{128} * 1024 * 1024;

// The part of a session's device bytes that is host RAM: all of them on a
// unified-memory device. Otherwise, where the memory type of buffers the
// host reads is not device-local, its host-read buffers and one twiddle
// table's staging copy (host_read_bytes); none where it is device-local.
constexpr std::size_t stage_host_memory(std::size_t device_bytes, std::size_t host_read_bytes,
                                        bool unified_memory, bool host_reads_in_host_ram) noexcept {
    return unified_memory ? device_bytes : host_reads_in_host_ram ? host_read_bytes : 0;
}

// Family entry points. A decline is the pure policy for a shape, or for
// the family alone with a null shape. Host bytes are the host memory a
// session of the shape can hold: its compilation (compiler_host_bound) and
// the part of its lease that is host RAM (stage_host_memory); zero when
// declined. A factory follows the session contract in
// src/triad_receiver_bulk.hpp and records its declines and fallbacks in the
// shared receipt. Qualified device: the family's automatic qualification
// covers this device class.

// --- signed-lag gain direct rank (src/triad_vulkan_gain.cpp) ---
bool vulkan_gain_rank_qualified_device(const Shared&) noexcept;
std::string_view vulkan_gain_rank_decline(const Shared&, const GainRankShape*, bool fp64_forced) noexcept;
std::size_t vulkan_gain_rank_host_bytes(const Shared&, const GainRankShape&, bool fp64_forced);
std::unique_ptr<GainRankBulk> make_vulkan_gain_rank_bulk(const std::shared_ptr<Shared>&, const GainRankShape&,
                                                         bool fp64_forced, std::stop_token);

// --- drift proposals (src/triad_vulkan_drift.cpp) ---
bool vulkan_drift_qualified_device(const Shared&) noexcept;
std::string_view vulkan_drift_decline(const Shared&, const DriftBulkShape*, bool fp64_forced) noexcept;
std::size_t vulkan_drift_host_bytes(const Shared&, const DriftBulkShape&, bool fp64_forced);
std::unique_ptr<DriftBulk> make_vulkan_drift_bulk(const std::shared_ptr<Shared>&, const DriftBulkShape&,
                                                  bool fp64_forced, std::stop_token);

// --- varying statistics (src/triad_vulkan_varying.cpp) ---
bool vulkan_varying_qualified_device(const Shared&) noexcept;
std::string_view vulkan_varying_decline(const Shared&, const VaryingBulkShape*, bool fp64_forced) noexcept;
std::size_t vulkan_varying_host_bytes(const Shared&, const VaryingBulkShape&, bool fp64_forced);
std::unique_ptr<VaryingBulk> make_vulkan_varying_bulk(const std::shared_ptr<Shared>&, const VaryingBulkShape&,
                                                      bool fp64_forced, std::stop_token);

} // namespace datapump::triad::v3::receiver_detail::vulkan
