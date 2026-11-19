#pragma once

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace datapump::triad::v3::receiver_detail {

struct OpenClNvfp4Workspace {
    std::size_t host_bytes = 0;
    std::size_t device_bytes = 0;
};

class OpenClNvfp4Device;

class OpenClNvfp4Projection {
public:
    ~OpenClNvfp4Projection();
    OpenClNvfp4Projection(const OpenClNvfp4Projection&) = delete;
    OpenClNvfp4Projection& operator=(const OpenClNvfp4Projection&) = delete;

    // The caller accounts separately for the mapped and projected host spans.
    static OpenClNvfp4Workspace workspace(std::size_t rows, std::size_t modes,
                                          std::size_t batch_capacity);

    // A reusable host slab for direct template mapping. Size counts complete
    // complex records including trailers. Empty means pageable input is used.
    std::span<std::complex<float>> mapped_staging(std::size_t samples) noexcept;

    // Both spans are lane-major. Each mapped lane contains rows samples then
    // their maximum absolute real or imaginary component in the real part of
    // one extra sample. Its imaginary part is zero. A nonfinite input header
    // is rejected. Output lanes contain the orthonormal DFT modes in bin order.
    // A return of false publishes no results.
    bool project(std::span<const std::complex<float>> mapped,
                 std::span<std::complex<float>> projected, std::stop_token stop = {});

private:
    struct Impl;
    explicit OpenClNvfp4Projection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class OpenClNvfp4Device;
};

class OpenClNvfp4Device {
public:
    ~OpenClNvfp4Device();
    OpenClNvfp4Device(const OpenClNvfp4Device&) = delete;
    OpenClNvfp4Device& operator=(const OpenClNvfp4Device&) = delete;

    // A nonzero UUID must match the selected Vulkan GPU. With no UUID, a
    // single eligible device is required. PTX is fixed at build time.
    static std::shared_ptr<OpenClNvfp4Device> open(
        std::span<const std::byte, 16> device_uuid, std::span<const std::byte> ptx,
        std::string* unavailable = nullptr);

    std::unique_ptr<OpenClNvfp4Projection> make_projection(
        std::size_t rows, std::span<const std::size_t> bins, std::size_t batch_capacity,
        std::size_t workspace_limit) const;

private:
    struct Impl;
    explicit OpenClNvfp4Device(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
    friend class OpenClNvfp4Projection;
};

} // namespace datapump::triad::v3::receiver_detail
