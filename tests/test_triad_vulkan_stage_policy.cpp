// Fine-stage device policy checks, with no device and in every build: the
// reason names and their lists, the predeclared selection order, stage
// shapes and routes, and the application GPU wrapper's forwarding of every
// fine-stage member (ctest triad_vulkan_stage_policy).

#include "datapump/triad_acceleration.hpp"
#include "datapump/triad_receiver.hpp"
#include "triad_receiver_bulk.hpp"
#include "triad_vulkan_stages.hpp"

#include <algorithm>
#include <array>
#include <complex>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace datapump::triad::v3::test {
namespace {
namespace r = receiver_detail;
namespace vk = receiver_detail::vulkan;
using r::StageFamily;
using r::StageReason;

void check(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

std::string expected_name(StageFamily family, StageReason reason) {
    return "vulkan-" + std::string(r::stage_family_reason_names[static_cast<std::size_t>(family)]) + "-" +
        std::string(r::stage_reason_suffix(reason));
}

void reason_names() {
    std::set<std::string> names;
    for (const auto family : r::stage_families)
        for (std::size_t i = 0; i < r::stage_reason_count; ++i) {
            const auto reason = static_cast<StageReason>(i);
            const auto text = vk::stage_reason_text(family, reason);
            check(text == expected_name(family, reason), "Vulkan stage reason name differs from its parts");
            check(names.insert(std::string(text)).second, "Vulkan stage reason names are not distinct");
        }
    // The suffix lists in StageReason order, and the reasons of one family.
    std::vector<std::string_view> suffixes(r::stage_policy_reasons.begin(), r::stage_policy_reasons.end());
    suffixes.insert(suffixes.end(), r::stage_fallback_reasons.begin(), r::stage_fallback_reasons.end());
    suffixes.insert(suffixes.end(), r::gain_rank_stage_reasons.begin(), r::gain_rank_stage_reasons.end());
    suffixes.insert(suffixes.end(), r::varying_stage_reasons.begin(), r::varying_stage_reasons.end());
    check(suffixes.size() == r::stage_reason_count, "Vulkan stage reason lists differ from StageReason");
    for (std::size_t i = 0; i < suffixes.size(); ++i) {
        const auto reason = static_cast<StageReason>(i);
        check(r::stage_reason_suffix(reason) == suffixes[i], "Vulkan stage reason suffix out of order");
        const bool gain = i >= r::stage_policy_reasons.size() + r::stage_fallback_reasons.size() &&
            i < r::stage_policy_reasons.size() + r::stage_fallback_reasons.size() + r::gain_rank_stage_reasons.size();
        const bool varying = i >= r::stage_reason_count - r::varying_stage_reasons.size();
        check(r::stage_reason_applies(StageFamily::gain_rank, reason) == !varying &&
              r::stage_reason_applies(StageFamily::drift, reason) == (!gain && !varying) &&
              r::stage_reason_applies(StageFamily::varying, reason) == !gain,
              "Vulkan stage reason applies to the wrong family");
        check(r::stage_reason_is_fallback(reason) ==
              (i >= r::stage_policy_reasons.size() && i < r::stage_policy_reasons.size() + r::stage_fallback_reasons.size()),
              "Vulkan stage fallback reasons differ from their list");
    }
}

// An independent statement of the predeclared order, as a list of
// conditions and their reasons, compared with the policy on every input.
std::optional<StageReason> reference(const vk::StagePolicy& p) {
    const std::array<std::pair<bool, StageReason>, 7> order{{
        {p.route == VulkanStageRoute::native, StageReason::native_requested},
        {p.fp64_forced, StageReason::fp64_requested},
        {p.host_scoring_forced, StageReason::host_scoring_forced},
        {!p.shader_fp64, StageReason::shader_fp64_unavailable},
        {!p.shape_supported, StageReason::unsupported_shape},
        {p.route == VulkanStageRoute::automatic && !p.automatic_qualified, StageReason::unqualified_device},
        {!p.implemented, StageReason::unimplemented}}};
    for (const auto& [applies, reason] : order)
        if (applies) return reason;
    return std::nullopt;
}

void policy_order() {
    // Every input: each decline reason is reported exactly when every
    // earlier condition passes.
    std::array<std::size_t, r::stage_policy_reasons.size()> seen{};
    std::size_t accepted = 0;
    for (const auto route : {VulkanStageRoute::automatic, VulkanStageRoute::device, VulkanStageRoute::native})
        for (unsigned bits = 0; bits < 64; ++bits) {
            vk::StagePolicy p;
            p.route = route;
            p.fp64_forced = bits & 1; p.host_scoring_forced = bits & 2; p.shader_fp64 = bits & 4;
            p.shape_supported = bits & 8; p.automatic_qualified = bits & 16; p.implemented = bits & 32;
            const auto actual = vk::stage_policy(p);
            check(actual == reference(p), "Vulkan stage policy differs from its predeclared order");
            if (actual) ++seen[static_cast<std::size_t>(*actual)];
            else ++accepted;
        }
    check(std::ranges::all_of(seen, [](std::size_t n) { return n > 0; }) && accepted > 0,
          "Vulkan stage policy check did not reach every outcome");
    // The chain itself: removing one failing condition at a time reports
    // the next reason, and both routes end accepted.
    vk::StagePolicy p{VulkanStageRoute::native, true, true, false, false, false, false};
    const auto expect = [&](std::optional<StageReason> reason, const char* message) {
        check(vk::stage_policy(p) == reason, message);
    };
    expect(StageReason::native_requested, "native route must decline first");
    p.route = VulkanStageRoute::device;
    expect(StageReason::fp64_requested, "explicit FP64 must decline before host scoring");
    p.fp64_forced = false;
    expect(StageReason::host_scoring_forced, "forced host scoring must decline before shader capability");
    p.host_scoring_forced = false;
    expect(StageReason::shader_fp64_unavailable, "missing FP64 shaders must decline before the shape");
    p.shader_fp64 = true;
    expect(StageReason::unsupported_shape, "an unsupported shape must decline before the route");
    p.shape_supported = true;
    expect(StageReason::unimplemented, "an explicit device route must not need automatic qualification");
    p.implemented = true;
    expect(std::nullopt, "an explicit device route must accept a supported shape");
    p.route = VulkanStageRoute::automatic;
    expect(StageReason::unqualified_device, "automatic selection must require qualification");
    p.automatic_qualified = true;
    expect(std::nullopt, "automatic selection must accept a qualified family");
    p.implemented = false;
    expect(StageReason::unimplemented, "a qualified family without an implementation must decline");
    p.automatic_qualified = false;
    expect(StageReason::unqualified_device, "qualification must decline before implementation");
}

void shapes_and_routes() {
    check(r::stage_family(r::StageShape{StageFamily::drift}) == StageFamily::drift &&
          r::stage_family(r::StageShape{r::GainRankShape{}}) == StageFamily::gain_rank &&
          r::stage_family(r::StageShape{r::DriftBulkShape{}}) == StageFamily::drift &&
          r::stage_family(r::StageShape{r::VaryingBulkShape{}}) == StageFamily::varying,
          "stage shapes name the wrong family");
    VulkanReceiverOptions options;
    check(options.gain_rank == VulkanStageRoute::automatic && options.drift_proposals == VulkanStageRoute::automatic &&
          options.varying_fine == VulkanStageRoute::automatic &&
          options.varying_arithmetic == VulkanVaryingArithmetic::automatic,
          "Vulkan fine-stage routes must default to automatic");
    options.gain_rank = VulkanStageRoute::device;
    options.drift_proposals = VulkanStageRoute::native;
    check(vk::stage_route(options, StageFamily::gain_rank) == VulkanStageRoute::device &&
          vk::stage_route(options, StageFamily::drift) == VulkanStageRoute::native &&
          vk::stage_route(options, StageFamily::varying) == VulkanStageRoute::automatic,
          "Vulkan fine-stage routes read the wrong option");
}

// Records every forwarded fine-stage call.
struct ForwardedCalls {
    std::vector<std::string> names;
    std::optional<r::StageShape> shape;
    bool fp64 = false;
    std::optional<r::StageNative> kind;
    std::optional<StageFamily> family;
    std::optional<StageReason> reason;
};
class FakeGainRank final : public r::GainRankBulk {
public:
    bool direct_ranks(const r::GainRankRequest&, std::span<double>, std::span<float>, r::StageDeviceWork&,
                      std::stop_token) override { return false; }
    r::BulkReceipt receipt() const override { return {}; }
};
class FakeDrift final : public r::DriftBulk {
public:
    std::span<std::complex<float>> staging(std::size_t) noexcept override { return {}; }
    bool ridge(const r::DriftAtomRequest&, r::DriftAtomResult&, r::StageDeviceWork&, std::stop_token) override {
        return false;
    }
    bool slopes(const r::DriftAtomRequest&, r::DriftAtomResult&, r::StageDeviceWork&, std::stop_token) override {
        return false;
    }
    r::BulkReceipt receipt() const override { return {}; }
};
class FakeVarying final : public r::VaryingBulk {
public:
    std::span<std::byte> template_staging(std::size_t) noexcept override { return {}; }
    bool statistics(const r::VaryingBatch&, r::VaryingBatchOutput&, r::StageDeviceWork&, std::stop_token) override {
        return false;
    }
    r::BulkReceipt receipt() const override { return {}; }
    std::size_t batch_blocks() const noexcept override { return 0; }
};
class FakeBackend final : public r::ReceiverBulkBackend {
public:
    explicit FakeBackend(ForwardedCalls& calls) : calls_(calls) {}
    std::unique_ptr<r::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<r::CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    r::BulkReceipt receipt() const override {
        r::BulkReceipt receipt;
        receipt.vulkan_drift.declined_calls = 7;
        return receipt;
    }
    std::string_view stage_decline(const r::StageShape& shape, bool fp64) const noexcept override {
        calls_.names.emplace_back("stage_decline");
        calls_.shape = shape; calls_.fp64 = fp64;
        return "fake-decline";
    }
    std::unique_ptr<r::GainRankBulk> make_gain_rank(const r::GainRankShape& shape, bool fp64,
                                                    std::stop_token) override {
        calls_.names.emplace_back("make_gain_rank");
        calls_.shape = shape; calls_.fp64 = fp64;
        return std::make_unique<FakeGainRank>();
    }
    std::unique_ptr<r::DriftBulk> make_drift(const r::DriftBulkShape& shape, bool fp64, std::stop_token) override {
        calls_.names.emplace_back("make_drift");
        calls_.shape = shape; calls_.fp64 = fp64;
        return std::make_unique<FakeDrift>();
    }
    std::unique_ptr<r::VaryingBulk> make_varying(const r::VaryingBulkShape& shape, bool fp64,
                                                 std::stop_token) override {
        calls_.names.emplace_back("make_varying");
        calls_.shape = shape; calls_.fp64 = fp64;
        return std::make_unique<FakeVarying>();
    }
    std::size_t stage_host_bytes(const r::StageShape& shape, bool fp64) const override {
        calls_.names.emplace_back("stage_host_bytes");
        calls_.shape = shape; calls_.fp64 = fp64;
        return 12'345;
    }
    void record_stage_native(StageFamily family, StageReason reason, r::StageNative kind) override {
        calls_.names.emplace_back("record_stage_native");
        calls_.family = family; calls_.reason = reason; calls_.kind = kind;
    }
private:
    ForwardedCalls& calls_;
};
// Only the pure virtual members: the base defaults of every stage member.
class MinimalBackend final : public r::ReceiverBulkBackend {
public:
    std::unique_ptr<r::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<r::CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    r::BulkReceipt receipt() const override { return {}; }
};

void selected_gpu_forwarding() {
    ForwardedCalls calls;
    const auto wrapped = r::make_selected_gpu_backend(std::make_shared<FakeBackend>(calls), "test selection");
    r::GainRankShape gain;
    gain.blocks = 3; gain.lags = 5; gain.tile_fft_rows = 256;
    r::DriftBulkShape drift;
    drift.rows = 11; drift.nfft = 16;
    r::VaryingBulkShape varying;
    varying.block_rows = 200; varying.arithmetic = r::VaryingArithmetic::fp32_template;
    const auto last = [&](const char* name) { return !calls.names.empty() && calls.names.back() == name; };
    check(wrapped->stage_decline(r::StageShape{drift}, true) == "fake-decline" && last("stage_decline") &&
          calls.fp64 && calls.shape && *calls.shape == r::StageShape{drift},
          "SelectedGpu does not forward stage_decline");
    check(wrapped->make_gain_rank(gain, false, {}) && last("make_gain_rank") && !calls.fp64 &&
          *calls.shape == r::StageShape{gain}, "SelectedGpu does not forward make_gain_rank");
    check(wrapped->make_drift(drift, true, {}) && last("make_drift") && calls.fp64 &&
          *calls.shape == r::StageShape{drift}, "SelectedGpu does not forward make_drift");
    check(wrapped->make_varying(varying, false, {}) && last("make_varying") &&
          *calls.shape == r::StageShape{varying}, "SelectedGpu does not forward make_varying");
    check(wrapped->stage_host_bytes(r::StageShape{StageFamily::varying}, true) == 12'345 &&
          last("stage_host_bytes") && calls.fp64 && *calls.shape == r::StageShape{StageFamily::varying},
          "SelectedGpu does not forward stage_host_bytes");
    const std::array<std::pair<StageReason, r::StageNative>, 3> records{{
        {StageReason::near_tie, r::StageNative::session_fallback},
        {StageReason::unified_memory_envelope, r::StageNative::fallback},
        {StageReason::verify_mode, r::StageNative::decline}}};
    for (const auto& [reason, kind] : records) {
        wrapped->record_stage_native(StageFamily::gain_rank, reason, kind);
        check(last("record_stage_native") && calls.family == StageFamily::gain_rank && calls.reason == reason &&
              calls.kind == kind, "SelectedGpu does not forward record_stage_native");
    }
    check(calls.names.size() == 8, "SelectedGpu forwarded an unexpected number of stage calls");
    const auto receipt = wrapped->receipt();
    check(receipt.vulkan_drift.declined_calls == 7 && receipt.selection_reason == "test selection",
          "SelectedGpu does not forward the stage receipt");
    // A backend without fine-stage sessions keeps every stage native.
    MinimalBackend minimal;
    check(minimal.stage_decline(r::StageShape{gain}, false) == "native-stage-backend" &&
          !minimal.make_gain_rank(gain, false, {}) && !minimal.make_drift(drift, false, {}) &&
          !minimal.make_varying(varying, false, {}) && minimal.stage_host_bytes(r::StageShape{drift}, false) == 0,
          "default fine-stage members must keep the native stage");
    minimal.record_stage_native(StageFamily::drift, StageReason::workspace, r::StageNative::fallback);
}

// The host-RAM part of a session's device bytes in every family's host
// quote: all of them on a unified-memory device, else the host-read part
// where host reads use host RAM (a discrete GPU's system-memory heap).
void session_host_memory() {
    static_assert(vk::stage_host_memory(1000, 40, true, false) == 1000 &&
                  vk::stage_host_memory(1000, 40, true, true) == 1000);
    check(vk::stage_host_memory(1000, 40, false, true) == 40,
          "host-read session buffers in host RAM must be quoted to the host preflight");
    check(vk::stage_host_memory(1000, 40, false, false) == 0,
          "device-local session buffers must not be quoted to the host preflight");
}

} // namespace
} // namespace datapump::triad::v3::test

int main() {
    namespace t = datapump::triad::v3::test;
    try {
        t::reason_names();
        t::policy_order();
        t::shapes_and_routes();
        t::selected_gpu_forwarding();
        t::session_host_memory();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "{\"schema\":\"datapump.triad.vulkan-checks.v1\",\"status\":\"checks_passed\","
                 "\"actual_gpu_work\":false,\"scope\":\"fine_stage_policy_reason_names_forwarding_and_host_quotes\"}\n";
    return 0;
}
