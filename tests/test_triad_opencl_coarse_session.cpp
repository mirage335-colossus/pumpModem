// The production coarse session through the dense FP4 route, on the kernel
// emulation device. Device-rendered and uploaded template banks must reach
// the proposals of the native search for the same public window, and a bank
// below the conditioning guard must leave the route before FP4 scores it.
#include "../src/triad_opencl_coarse_bulk.hpp"
#include "../src/triad_opencl_nvfp4_projection.hpp"
#include "../src/triad_receiver_coarse.hpp"
#include "datapump/host/sandbox.hpp"
#include "triad_crypto_fixture.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

v::Complex rotation(long double cycles) {
    return std::polar(1., static_cast<double>(2 * std::numbers::pi_v<long double> * std::remainder(cycles, 1.L)));
}

class Host final : public r::OpenClCoarseHost {
public:
    std::size_t workspace_limit() const override { return limit; }
    void acquire(std::size_t bytes, std::stop_token) override {
        std::lock_guard lock(mutex_);
        check(bytes <= limit - leased_, "emulated coarse session exceeded its device workspace");
        leased_ += bytes;
        peak = std::max(peak, leased_);
    }
    void release(std::size_t bytes) noexcept override {
        std::lock_guard lock(mutex_);
        leased_ -= bytes;
    }
    void account(const r::BulkReceipt& delta) override {
        std::lock_guard lock(mutex_);
        total_.score_rows += delta.score_rows;
        total_.executed |= delta.executed;
        r::add_opencl_coarse_work(total_, delta);
    }
    r::BulkReceipt receipt() const override {
        std::lock_guard lock(mutex_);
        return total_;
    }
    std::size_t leased() const {
        std::lock_guard lock(mutex_);
        return leased_;
    }

    std::size_t limit = std::size_t{1} << 30, peak = 0;

private:
    mutable std::mutex mutex_;
    r::BulkReceipt total_;
    std::size_t leased_ = 0;
};

struct Delegated : std::runtime_error {
    Delegated() : std::runtime_error("the search reached its fallback session") {}
};

// Stands in for the FFT session of automatic selection. Reaching its
// preparation with the native columns is the outcome under test.
class FallbackProbe final : public r::CoarseBulk {
public:
    bool supports_fir() const noexcept override { return false; }
    std::shared_ptr<const r::CoarseBulkFilter> prepare_filter(std::span<const double>, std::size_t,
        r::CoarseBulkWork&, std::stop_token) override {
        throw std::logic_error("fallback probe has no device FIR");
    }
    std::vector<v::Complex> project_fir(const r::CoarseBulkFilter&, const r::CoarseFirCrop&,
        const r::CoarseFirSource&, r::CoarseBulkWork&, std::stop_token) override {
        throw std::logic_error("fallback probe has no device FIR");
    }
    void prepare(const r::CoarseBulkGeometry& geometry, std::span<const v::Complex> observation,
        std::span<const r::CoarseBulkWorkspace> bank, std::span<const r::CoarseZoomGroup>,
        r::CoarseBulkWork&, std::stop_token) override {
        check(geometry.minimum_eigenvalue_ratio < 1. / 16 && !observation.empty() && !bank.empty() &&
              std::ranges::all_of(bank, [&](const auto& row) {
                  return row.columns[0].size() == geometry.kernel_rows && row.columns[1].size() == geometry.kernel_rows;
              }), "fallback session did not receive the native columns of an ill-conditioned bank");
        throw Delegated();
    }
    void frequency_tile(std::span<const double>, const r::CoarseScoreSink&, r::CoarseBulkWork&,
                        std::stop_token) override {
        throw std::logic_error("fallback probe scores nothing");
    }
    void lag_tile(std::size_t, std::size_t, const r::CoarseScoreSink&, r::CoarseBulkWork&,
                  std::stop_token) override {
        throw std::logic_error("fallback probe scores nothing");
    }
    r::BulkReceipt receipt() const override { return {}; }
};

// required: the explicit route. delegated: a session that neither renders
// nor uploads, over a required one. automatic: the options of automatic
// selection, over the fallback probe.
enum class Route { required, delegated, automatic };

class Backend final : public r::ReceiverBulkBackend {
public:
    Backend(std::shared_ptr<Host> host, std::shared_ptr<const r::OpenClNvfp4Device> device, bool synthesize,
            Route route)
        : host_(std::move(host)), device_(std::move(device)), synthesize_(synthesize), route_(route) {}
    bool supports_fine() const noexcept override { return false; }
    std::unique_ptr<r::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<r::CoarseBulk> make_coarse(double, double) override {
        if (route_ == Route::automatic)
            return r::make_opencl_coarse_bulk(host_, device_, std::make_unique<FallbackProbe>(),
                                              {false, false, true, false});
        auto required = r::make_opencl_coarse_bulk(host_, device_, {}, {true, true, synthesize_});
        if (route_ == Route::required) return required;
        // An automatic session that neither renders nor uploads a bank
        // hands every stage of the search to its fallback.
        return r::make_opencl_coarse_bulk(host_, device_, std::move(required), {false, false, false, false});
    }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return 0; }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 1 << 20; }
    r::BulkReceipt receipt() const override { return host_->receipt(); }

private:
    std::shared_ptr<Host> host_;
    std::shared_ptr<const r::OpenClNvfp4Device> device_;
    bool synthesize_;
    Route route_;
};

struct Fixture {
    v::ReceiverConfig config;
    v::ReceiverSlotPlan slot;
    std::vector<v::Complex> iq;
    double arrival = .025, frequency = .04;
    std::int64_t epoch = 2;
    std::uint8_t bit = 1;
};

// A sampled two-ray observation with a clock offset, in receiver noise.
Fixture capture(const datapump::Crypto& crypto, double noise_sigma, double spread_delay_seconds = .0145,
                double spread_doppler_hz = 1.25) {
    Fixture f;
    auto& c = f.config;
    auto& s = f.slot;
    c.sample_rate_hz = 600;
    c.noise_variance = .05;
    c.frequency_bound_hz = .2;
    c.clock_bound_ppm = 400;
    c.spread_delay_seconds = spread_delay_seconds;
    c.spread_doppler_hz = spread_doppler_hz;
    s.first_sample = 80'000;
    s.window_capture_lead_seconds = -29.4L;
    s.source_start_seconds = 30;
    s.arrival_lower_seconds = -.1;
    s.arrival_upper_seconds = .1;
    s.format.sample_rate = 600;
    s.format.inner_rate_hz = 36;
    s.format.added_symbols = 4;
    s.format.bit_seconds = .1;
    s.format.symbol_ordinal = 7;
    auto format = s.format;
    format.epoch += f.epoch;
    v::PatternSource source(format, crypto, f.bit);
    const auto count = source.geometry().source_samples + 720;
    s.stop_sample = s.first_sample + count;
    f.iq.resize(count);
    std::mt19937 random(4711);
    std::normal_distribution<double> noise(0, noise_sigma);
    for (std::size_t i = 0; i < f.iq.size(); ++i) {
        const auto t = static_cast<long double>(i) / c.sample_rate_hz - s.window_capture_lead_seconds;
        const auto position = [&](double delay) {
            return ((t - f.arrival - delay) * (1 + c.clock_bound_ppm * 1e-6) - s.source_start_seconds) * c.sample_rate_hz;
        };
        auto value = v::Complex(.8, .2) * source.sample_at(position(0));
        value += v::Complex(.3, -.6) * source.sample_at(position(c.spread_delay_seconds)) *
                 rotation(c.spread_doppler_hz * t);
        f.iq[i] = value * rotation(f.frequency * t) + v::Complex(noise(random), noise(random));
    }
    return f;
}

const r::CoarseProposal& winner(const r::CoarseResult& result) {
    check(result.proposal_count >= 2 && !result.coordinates.empty(), "coarse search lost a bit alternative");
    return *std::max_element(result.proposals.begin(), result.proposals.begin() + result.proposal_count,
        [](const auto& a, const auto& b) { return a.score < b.score; });
}

void check_session(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& crypto,
                   double noise_sigma, bool synthesize, bool declined = false) {
    auto f = capture(crypto, noise_sigma);
    // One narrow public interval keeps the emulated tile count small.
    const std::array<r::FrequencyInterval, 1> prior{{{.03125, .045}}};
    f.config.helper_enabled = true;
    constexpr unsigned workers = 3;
    f.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    const auto native = r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers,
                                        r::coarse_workspace(f.config, f.slot, workers), {});
    f.config.native_coarse_precision = v::NativeCoarsePrecision::automatic;
    const auto host = std::make_shared<Host>();
    f.config.bulk_backend = std::make_shared<Backend>(host, device, synthesize,
                                                      declined ? Route::delegated : Route::required);
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    const auto dense = r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers, limit, {});
    check(host->leased() == 0 && host->peak > 0, "coarse FP4 session kept or never took its device lease");
    check(host->receipt().opencl_coarse_frequency_fallbacks == (declined ? 1u : 0u) &&
          host->receipt().opencl_coarse_sessions == 1,
          "coarse FP4 session miscounted its fallback");

    const auto& plan = dense.work.plan;
    const auto score_rows = plan.workspaces * plan.frequency_points * plan.arrival_points;
    check(plan.route == r::CoarseRoute::frequency_tiles && plan.workspaces == 78 && plan.rays == 2 &&
          plan.frequency_points == native.work.plan.frequency_points &&
          plan.arrival_points == native.work.plan.arrival_points && plan.arrival_points >= 16,
          "coarse FP4 session changed the public search geometry");
    check(dense.work.bulk && dense.work.bulk->executed && dense.work.bulk->score_rows == score_rows &&
          dense.work.bulk->opencl_coarse_score_rows == score_rows &&
          dense.work.search.operations.score_rows == score_rows &&
          dense.work.bulk->opencl_coarse_issued_products >= dense.work.bulk->opencl_coarse_useful_products &&
          dense.work.bulk->opencl_coarse_useful_products == score_rows * 8 * plan.projected_template_rows,
          "coarse FP4 session did not credit every public score row");
    const auto columns = plan.workspaces * plan.rays;
    check(dense.work.bulk->opencl_coarse_synthesized_columns == (synthesize ? columns : 0) &&
          dense.work.bulk->opencl_coarse_uploaded_columns == (synthesize ? 0 : columns),
          "coarse FP4 session used the wrong template source");
    check(plan.precision_reason == (synthesize ? "opencl-nvfp4-dense-device-templates-fp32-rescored"
                                               : "opencl-nvfp4-dense-fp32-rescored"),
          "coarse FP4 session reported another arithmetic route");
    if (synthesize)
        check(dense.work.template_bank.operations.projected_columns == columns &&
              dense.work.template_bank.operations.source_render_rows == 0 &&
              dense.work.bulk->opencl_coarse_upload_bytes < 4 * plan.projected_observation_rows * 16 + (1u << 20),
              "device template synthesis still rendered or uploaded the bank on the host");

    const auto& expected = winner(native);
    const auto& actual = winner(dense);
    check(expected.bit == f.bit && expected.coordinate.epoch_offset_seconds == f.epoch,
          "native coarse fixture lost its planted alternative");
    check(actual.bit == expected.bit && actual.coordinate == expected.coordinate && actual.rank == expected.rank,
          "coarse FP4 winner differs from the native search");
    check(std::abs(actual.score - expected.score) <= 2e-4 * expected.score,
          "coarse FP4 rescored winner differs from the native FP64 score");
    check(dense.coordinates.size() == native.coordinates.size() && dense.proposal_count == native.proposal_count,
          "coarse FP4 session changed the number of refined coordinates");
    std::cout << "OpenCL coarse session, " << (declined ? "fallback with " : "")
              << (synthesize ? "device" : "uploaded") << " templates, noise "
              << noise_sigma << ": winner score " << actual.score << " (native " << expected.score << "), "
              << score_rows << " score rows\n";
}

// Two nearly collinear rays. The FP4 arrival frontier is not trusted below
// the canonical conditioning guard, whatever supplied the columns.
void check_conditioning(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& crypto) {
    auto f = capture(crypto, 0, 5e-6, .001);
    const std::array<r::FrequencyInterval, 1> prior{{{.03125, .045}}};
    f.config.helper_enabled = true;
    constexpr unsigned workers = 3;
    const auto score = [&] {
        (void)r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers,
                              r::coarse_workspace(f.config, f.slot, workers), {});
    };
    {
        // Automatic selection renders the bank, reads its Gram and hands the
        // search to its fallback with native columns.
        const auto host = std::make_shared<Host>();
        f.config.bulk_backend = std::make_shared<Backend>(host, device, true, Route::automatic);
        bool delegated = false;
        try { score(); } catch (const Delegated&) { delegated = true; }
        const auto receipt = host->receipt();
        check(delegated && host->leased() == 0 && host->peak > 0 && receipt.opencl_coarse_frequency_fallbacks == 1 &&
              receipt.opencl_coarse_score_rows == 0 && receipt.opencl_coarse_synthesized_columns == 0 &&
              receipt.opencl_coarse_sessions == 0 && receipt.opencl_coarse_dispatches > 0,
              "ill-conditioned bank was not handed to the fallback before FP4 scoring");
    }
    for (const bool synthesize : {true, false}) {
        // A required session reports instead of scoring.
        const auto host = std::make_shared<Host>();
        f.config.bulk_backend = std::make_shared<Backend>(host, device, synthesize, Route::required);
        std::string reason;
        try { score(); } catch (const v::ReceiverWorkspaceError& error) { reason = error.what(); }
        check(reason.find("better conditioned") != std::string::npos && host->leased() == 0 &&
              host->receipt().opencl_coarse_score_rows == 0,
              "required coarse FP4 session scored an ill-conditioned bank");
    }
    std::cout << "OpenCL coarse session left an ill-conditioned bank to its fallback and reported it when required\n";
}

} // namespace

int main() {
    try {
        constexpr std::array descriptors{0, 1, 2};
        datapump::host::restrict_descriptors(descriptors);
        datapump::host::install_no_socket_boundary();
        std::array<std::uint8_t, 32> seed{};
        for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(31 * i + 11);
        const auto crypto = triad_test_key(seed);
        const std::array<std::byte, 16> any_uuid{};
        std::string reason;
        const auto device = r::OpenClNvfp4Device::open(any_uuid, std::as_bytes(std::span(r::opencl_nvfp4_ptx)), &reason);
        if (!device) throw std::runtime_error(reason);
        check_session(device, crypto, 0, false);
        check_session(device, crypto, .12, true);
        check_session(device, crypto, 0, false, true);
        check_conditioning(device, crypto);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
