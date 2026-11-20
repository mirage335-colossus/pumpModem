#pragma once

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
};

// A frequency-tile session that scores every public workspace, frequency and
// arrival on the FP4 Tensor Cores and returns exact arrival frontiers with
// FP32 rescored peaks. Geometry outside its envelope, and a template bank
// below the canonical 1/16 eigenvalue-ratio guard, go to the fallback before
// any FP4 score is formed.
std::unique_ptr<CoarseBulk> make_opencl_coarse_bulk(std::shared_ptr<OpenClCoarseHost>,
    std::shared_ptr<const OpenClNvfp4Device>, std::unique_ptr<CoarseBulk> fallback, const OpenClCoarseOptions&);

// Adds this route's counters, as OpenClCoarseHost::account needs.
void add_opencl_coarse_work(BulkReceipt& to, const BulkReceipt& from) noexcept;

} // namespace datapump::triad::v3::receiver_detail
