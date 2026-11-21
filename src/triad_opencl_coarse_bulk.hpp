#pragma once

#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_receiver_bulk.hpp"

#include <memory>

namespace datapump::triad::v3::receiver_detail {

class OpenClNvfp4Device;

// The device workspace and receipts of the backend that owns a session.
class OpenClCoarseHost {
public:
    virtual ~OpenClCoarseHost() = default;
    virtual std::size_t workspace_limit() const = 0;
    // Blocks until the shared device workspace admits this session.
    virtual void acquire(std::size_t device_bytes, std::stop_token) = 0;
    virtual void release(std::size_t device_bytes) noexcept = 0;
    // Adds completed work to the backend totals.
    virtual void account(const BulkReceipt& delta) = 0;
    // Backend identity and totals, without this session's own counters.
    virtual BulkReceipt receipt() const = 0;
};

struct OpenClCoarseOptions {
    // A required route reports unsupported geometry instead of falling back.
    bool required = false;
    // Record device command intervals in the receipt.
    bool profile = false;
    // Render the bank from its keyed coefficients when the session offers it.
    bool synthesize = true;
    // Also score banks whose channelized columns the host prepared.
    bool uploaded_banks = true;
    // Accumulation of the rescored retained arrivals. The Vulkan backend
    // sets it from VulkanReceiverOptions::opencl_coarse_fp32_rescore (FP32
    // partial sums by default); FP64 here is only this layer's control.
    OpenClRescoreArithmetic rescore = OpenClRescoreArithmetic::fp64;
    // Admit one-ray sessions on an automatic route, within the measured
    // dense cost bound. A required route always admits them. The Vulkan
    // backend sets it from VulkanReceiverOptions::opencl_coarse_one_ray, on
    // by default since the RTX PRO 6000 qualification of 20 November 2026;
    // false here is only this layer's default.
    bool one_ray = false;
};

// A frequency-tile session that scores every public workspace, frequency and
// arrival on the FP4 Tensor Cores and returns exact arrival frontiers with
// FP32 rescored peaks. Geometry outside its envelope, and a template bank
// below the canonical 1/16 eigenvalue-ratio guard, go to the fallback before
// any FP4 score is formed. A required session never hands it frequencies.
// Either way the fallback scores the changing-channel segmented search.
// Receipts name each fallback's reason.
std::unique_ptr<CoarseBulk> make_opencl_coarse_bulk(std::shared_ptr<OpenClCoarseHost>,
    std::shared_ptr<const OpenClNvfp4Device>, std::unique_ptr<CoarseBulk> fallback, const OpenClCoarseOptions&);

// Adds this route's counters, as OpenClCoarseHost::account needs.
void add_opencl_coarse_work(BulkReceipt& to, const BulkReceipt& from) noexcept;

} // namespace datapump::triad::v3::receiver_detail
