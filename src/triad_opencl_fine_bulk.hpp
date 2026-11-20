#pragma once

#include "triad_opencl_nvfp4_projection.hpp"
#include "triad_receiver_bulk.hpp"

#include <memory>

namespace datapump::triad::v3::receiver_detail {

class OpenClNvfp4Device;

// The device workspace and receipts of the backend that owns a fine session.
class OpenClFineHost {
public:
    virtual ~OpenClFineHost() = default;
    // Leases device workspace without waiting. False leaves nothing held.
    virtual bool try_lease(std::size_t device_bytes) = 0;
    virtual void release(std::size_t device_bytes) noexcept = 0;
    // Adds completed work to the backend totals, as add_opencl_fine_work does.
    virtual void account(const BulkReceipt& delta) = 0;
    virtual BulkReceipt receipt() const = 0;
};

// One resident batch holds this many public blocks unless a caller chooses.
inline constexpr std::size_t opencl_fine_default_window_blocks = 16;
inline constexpr std::size_t opencl_fine_maximum_window_blocks = 64;

struct OpenClFineOptions {
    OpenClFineMethod method = OpenClFineMethod::resident_fp4;
    // A required staged projection reports a workspace it cannot lease
    // instead of taking the native route.
    bool required = false;
    // Record host spans and device command intervals in the receipt.
    bool profile = false;
    // Render dense source tiles on the device from keyed coefficients. Off
    // uploads the native tiles, as a matched control.
    bool device_tiles = true;
    // Public blocks of one resident batch. Two keeps one dispatch a batch.
    std::size_t window_blocks = opencl_fine_default_window_blocks;
};

// Fine statistics of the isolated 1500-row, 47-mode, two-path DATA geometry
// on the SM120 OpenCL module. Resident methods map, project and reduce on the
// device. Other blocks keep the native route through the host projection.
std::unique_ptr<FineBulk> make_opencl_fine_bulk(std::shared_ptr<OpenClFineHost>,
    std::shared_ptr<const OpenClNvfp4Device>, const OpenClFineOptions&);

// Public-geometry quotes for the owning backend's preflight.
bool opencl_fine_supports(const FineBulkGeometry&) noexcept;
std::size_t opencl_fine_resident_blocks(const OpenClFineOptions&) noexcept;
std::size_t opencl_fine_host_bytes(const OpenClFineOptions&);

// Adds this route's counters, as OpenClFineHost::account needs.
void add_opencl_fine_work(BulkReceipt& to, const BulkReceipt& from);

} // namespace datapump::triad::v3::receiver_detail
