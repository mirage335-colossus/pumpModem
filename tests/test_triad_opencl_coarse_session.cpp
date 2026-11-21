// The production coarse session through the dense FP4 route, on the kernel
// emulation device. Device-rendered and uploaded template banks must reach
// the proposals of the native search for the same public window, and a bank
// below the conditioning guard must leave the route before FP4 scores it.
// "one-ray" instead runs one-ray sessions through the automatic options,
// with and without one-ray admission, and the required route, and the named
// fallback reasons of declined sessions.
#include "../src/triad_opencl_coarse_bulk.hpp"
#include "../src/triad_opencl_nvfp4_projection.hpp"
#include "../src/triad_receiver_coarse.hpp"
#include "datapump/host/sandbox.hpp"
#include "triad_crypto_fixture.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
    std::size_t workspace_limit() const override {
        if (exhausted) throw std::bad_alloc();
        return limit;
    }
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
    // The limit query fails as a host allocation would.
    bool exhausted = false;

private:
    mutable std::mutex mutex_;
    r::BulkReceipt total_;
    std::size_t leased_ = 0;
};

struct Delegated : std::runtime_error {
    Delegated() : std::runtime_error("the search reached its fallback session") {}
};

// What the last fallback preparation received: the bank's conditioning and
// whether every workspace carried its rays' native columns and no others.
struct Received {
    double minimum_eigenvalue_ratio = 0;
    std::size_t taps = 0, arrivals = 0;
    bool native_columns = false;
};
Received received;

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
        received = {geometry.minimum_eigenvalue_ratio, geometry.taps, geometry.lag_count,
                    !observation.empty() && !bank.empty() && std::ranges::all_of(bank, [&](const auto& row) {
                        return row.columns[0].size() == geometry.kernel_rows &&
                               row.columns[1].size() == (geometry.taps == 2 ? geometry.kernel_rows : 0);
                    })};
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

// Stands in for the FFT session's changing-channel segmented stage, which
// the dense session forwards whichever route scores the frequencies.
class SegmentedProbe final : public r::CoarseBulk {
public:
    std::size_t planned = 0, calls = 0;
    bool begun = false, ended = false;
    bool supports_fir() const noexcept override { return false; }
    void plan_segmented(std::span<const r::CoarseSegmentedShape> shapes) override { planned = shapes.size(); }
    void begin_segmented(std::stop_token) override { begun = true; }
    void end_segmented() noexcept override { ended = true; }
    bool segmented_scores(const r::CoarseSegmentedShape&, std::span<const std::complex<float>>,
        std::span<const v::Complex>, double, std::span<double> fixed, std::span<double> smooth,
        r::CoarseBulkWork& work, std::stop_token) override {
        ++calls;
        std::ranges::fill(fixed, 7.);
        std::ranges::fill(smooth, 9.);
        work.inverse_ffts = 1;
        return true;
    }
    std::shared_ptr<const r::CoarseBulkFilter> prepare_filter(std::span<const double>, std::size_t,
        r::CoarseBulkWork&, std::stop_token) override {
        throw std::logic_error("segmented probe has no device FIR");
    }
    std::vector<v::Complex> project_fir(const r::CoarseBulkFilter&, const r::CoarseFirCrop&,
        const r::CoarseFirSource&, r::CoarseBulkWork&, std::stop_token) override {
        throw std::logic_error("segmented probe has no device FIR");
    }
    void prepare(const r::CoarseBulkGeometry&, std::span<const v::Complex>, std::span<const r::CoarseBulkWorkspace>,
                 std::span<const r::CoarseZoomGroup>, r::CoarseBulkWork&, std::stop_token) override {
        throw std::logic_error("segmented probe scores no frequencies");
    }
    void frequency_tile(std::span<const double>, const r::CoarseScoreSink&, r::CoarseBulkWork&,
                        std::stop_token) override {
        throw std::logic_error("segmented probe scores no frequencies");
    }
    void lag_tile(std::size_t, std::size_t, const r::CoarseScoreSink&, r::CoarseBulkWork&,
                  std::stop_token) override {
        throw std::logic_error("segmented probe scores no frequencies");
    }
    r::BulkReceipt receipt() const override {
        r::BulkReceipt result;
        result.vulkan_segmented_sessions = calls;
        result.vulkan_segmented_planned_shapes = planned;
        result.upload_bytes = 11 * calls;
        result.download_bytes = 5 * calls;
        result.synchronizations = calls;
        result.vulkan_device_command_seconds = .25 * static_cast<double>(calls);
        return result;
    }
};

// required: the explicit route. delegated: a session that neither renders
// nor uploads, over a required one. automatic: the options of automatic
// selection, over the fallback probe, with one-ray admission when stated.
enum class Route { required, delegated, automatic };

class Backend final : public r::ReceiverBulkBackend {
public:
    Backend(std::shared_ptr<Host> host, std::shared_ptr<const r::OpenClNvfp4Device> device, bool synthesize,
            Route route, r::OpenClRescoreArithmetic rescore = r::OpenClRescoreArithmetic::fp64, bool one_ray = false)
        : host_(std::move(host)), device_(std::move(device)), synthesize_(synthesize), route_(route),
          rescore_(rescore), one_ray_(one_ray) {}
    bool supports_fine() const noexcept override { return false; }
    std::unique_ptr<r::FineBulk> make_fine(double, double) override { return {}; }
    std::unique_ptr<r::CoarseBulk> make_coarse(double, double) override {
        if (route_ == Route::automatic)
            return r::make_opencl_coarse_bulk(host_, device_, std::make_unique<FallbackProbe>(),
                                              {false, false, true, false, r::OpenClRescoreArithmetic::fp64, one_ray_});
        auto required = r::make_opencl_coarse_bulk(host_, device_, {}, {true, true, synthesize_, true, rescore_});
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
    r::OpenClRescoreArithmetic rescore_;
    bool one_ray_;
};

struct Fixture {
    v::ReceiverConfig config;
    v::ReceiverSlotPlan slot;
    std::vector<v::Complex> iq;
    double arrival = .025, frequency = .04;
    std::int64_t epoch = 2;
    std::uint8_t bit = 1;
};

// A sampled two-ray observation with a clock offset, in receiver noise. With
// no spread, one ray.
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
        if (c.spread_delay_seconds || c.spread_doppler_hz)
            value += v::Complex(.3, -.6) * source.sample_at(position(c.spread_delay_seconds)) *
                     rotation(c.spread_doppler_hz * t);
        f.iq[i] = value * rotation(f.frequency * t) + v::Complex(noise(random), noise(random));
    }
    return f;
}

std::size_t fallback(std::string_view reason) {
    const auto found = std::ranges::find(r::opencl_coarse_fallback_reasons, reason);
    check(found != r::opencl_coarse_fallback_reasons.end(), "unknown coarse fallback reason");
    return static_cast<std::size_t>(found - r::opencl_coarse_fallback_reasons.begin());
}

bool only_fallback(const r::BulkReceipt& receipt, std::string_view reason) {
    const auto index = fallback(reason);
    for (std::size_t i = 0; i < receipt.opencl_coarse_fallback_counts.size(); ++i)
        if (receipt.opencl_coarse_fallback_counts[i] != (i == index ? 1u : 0u)) return false;
    return receipt.opencl_coarse_frequency_fallbacks == 1;
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
    // The synthesized session rescores with FP32 partial sums, the uploaded
    // ones with FP64 accumulation, so both arithmetics meet the native search.
    const auto rescore = synthesize ? r::OpenClRescoreArithmetic::fp32_partial : r::OpenClRescoreArithmetic::fp64;
    f.config.bulk_backend = std::make_shared<Backend>(host, device, synthesize,
                                                      declined ? Route::delegated : Route::required, rescore);
    const auto limit = r::coarse_workspace(f.config, f.slot, workers);
    const auto dense = r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers, limit, {});
    check(host->leased() == 0 && host->peak > 0, "coarse FP4 session kept or never took its device lease");
    check(host->receipt().opencl_coarse_frequency_fallbacks == (declined ? 1u : 0u) &&
          host->receipt().opencl_coarse_fallback_counts[fallback("device-templates-off")] == (declined ? 1u : 0u) &&
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
    check(plan.precision_reason == (synthesize ? "opencl-nvfp4-dense-device-templates-fp32-partial-rescored"
                                               : "opencl-nvfp4-dense-fp32-rescored"),
          "coarse FP4 session reported another arithmetic route");
    const auto rescored = dense.work.bulk->opencl_coarse_rescored_peaks;
    check(rescored > 0 && dense.work.bulk->opencl_coarse_rescore_fp32_partial_peaks == (synthesize ? rescored : 0) &&
          dense.work.bulk->opencl_coarse_rescore_fp64_peaks == (synthesize ? 0 : rescored),
          "coarse FP4 session did not name the accumulation of its rescored peaks");
    if (synthesize)
        check(dense.work.template_bank.operations.projected_columns == columns &&
              dense.work.template_bank.operations.source_render_rows == 0 &&
              dense.work.bulk->opencl_coarse_upload_bytes < 4 * plan.projected_observation_rows * 16 + (1u << 20),
              "device template synthesis still rendered or uploaded the bank on the host");
    // This FIR's taps fit one stage window of a tile, so it warps the raw
    // rows that it stages and the session leases no stored raw columns.
    if (synthesize)
        check(host->peak < columns * plan.raw_template_rows * sizeof(std::complex<float>),
              "device template synthesis leased stored raw columns");

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
              << (synthesize ? "device templates, FP32 partial" : "uploaded templates, FP64") << " rescoring, noise "
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
        check(delegated && received.native_columns && received.taps == 2 &&
              received.minimum_eigenvalue_ratio < 1. / 16 && host->leased() == 0 && host->peak > 0 &&
              receipt.opencl_coarse_frequency_fallbacks == 1 &&
              receipt.opencl_coarse_fallback_counts[fallback("ill-conditioned-template")] == 1 &&
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

// Automatic and required dense sessions both hand the segmented stage to
// their FFT session; without one the native scorer keeps it.
void check_segmented_forwarding(const std::shared_ptr<r::OpenClNvfp4Device>& device) {
    const r::CoarseSegmentedShape shape{64, 4, 2, 16, 3, 1, 1, 1};
    const std::vector<std::complex<float>> spectra(shape.blocks * shape.phases * shape.fft_rows);
    const std::vector<v::Complex> column(shape.kernel_rows, v::Complex{1});
    for (const bool required : {false, true}) {
        const auto host = std::make_shared<Host>();
        auto probe = std::make_unique<SegmentedProbe>();
        const auto* seen = probe.get();
        auto session = r::make_opencl_coarse_bulk(host, device, std::move(probe), {required, false, true, required});
        session->plan_segmented(std::span(&shape, 1));
        session->begin_segmented({});
        std::vector<double> fixed(shape.arrivals), smooth(shape.arrivals);
        r::CoarseBulkWork work;
        const bool scored = session->segmented_scores(shape, spectra, column, 1., fixed, smooth, work, {});
        session->end_segmented();
        check(scored && seen->planned == 1 && seen->begun && seen->ended && seen->calls == 1 &&
              std::ranges::all_of(fixed, [](double x) { return x == 7.; }) &&
              std::ranges::all_of(smooth, [](double x) { return x == 9.; }) && work.inverse_ffts == 1 &&
              host->leased() == 0, "dense coarse session did not forward the segmented stage to its FFT session");
        // The session receipt carries the segmented work and transfers.
        const auto receipt = session->receipt();
        check(receipt.vulkan_segmented_sessions == 1 && receipt.vulkan_segmented_planned_shapes == 1 &&
              receipt.executed && receipt.upload_bytes == 11 && receipt.download_bytes == 5 &&
              receipt.synchronizations == 1 && receipt.vulkan_device_command_seconds == .25,
              "dense coarse session receipt dropped its segmented work or transfers");
    }
    const auto host = std::make_shared<Host>();
    auto bare = r::make_opencl_coarse_bulk(host, device, {}, {true, false, true, true});
    bare->plan_segmented(std::span(&shape, 1));
    bare->begin_segmented({});
    std::vector<double> fixed(shape.arrivals, -1.), smooth(shape.arrivals, -1.);
    r::CoarseBulkWork work;
    check(!bare->segmented_scores(shape, spectra, column, 1., fixed, smooth, work, {}) && work == r::CoarseBulkWork{} &&
          std::ranges::all_of(fixed, [](double x) { return x == -1.; }),
          "a dense session without an FFT session changed segmented outputs");
    bare->end_segmented();
    std::cout << "OpenCL coarse session forwarded the segmented stage to its FFT session\n";
}

// The GUI's default spread is none: one ray. Automatic options with one-ray
// admission and the required route render its bank on the device, or the
// required route scores host-prepared columns when it does not render. Each
// scores four workspaces per bank tile and reaches the native search's
// proposals with no fallback.
void check_one_ray_sessions(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& crypto,
                            double noise_sigma) {
    auto f = capture(crypto, noise_sigma, 0, 0);
    const std::array<r::FrequencyInterval, 1> prior{{{.03125, .045}}};
    f.config.helper_enabled = true;
    constexpr unsigned workers = 3;
    f.config.native_coarse_precision = v::NativeCoarsePrecision::fp64;
    const auto native = r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers,
                                        r::coarse_workspace(f.config, f.slot, workers), {});
    f.config.native_coarse_precision = v::NativeCoarsePrecision::automatic;
    struct Variant {
        const char* name;
        Route route;
        bool synthesize;
        r::OpenClRescoreArithmetic rescore;
        std::string_view reason;
    };
    using enum r::OpenClRescoreArithmetic;
    for (const Variant& variant : {
             Variant{"automatic options with one-ray admission", Route::automatic, true, fp64,
                     "opencl-nvfp4-dense-device-templates-fp32-rescored"},
             Variant{"required, device templates, FP32 partial rescoring", Route::required, true, fp32_partial,
                     "opencl-nvfp4-dense-device-templates-fp32-partial-rescored"},
             Variant{"required, uploaded templates", Route::required, false, fp64, "opencl-nvfp4-dense-fp32-rescored"}}) {
        const auto host = std::make_shared<Host>();
        f.config.bulk_backend = std::make_shared<Backend>(host, device, variant.synthesize, variant.route,
                                                          variant.rescore, true);
        received = {};
        const auto dense = r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers,
                                           r::coarse_workspace(f.config, f.slot, workers), {});
        const auto receipt = host->receipt();
        check(received.taps == 0 && host->leased() == 0 && host->peak > 0 && receipt.opencl_coarse_sessions == 1 &&
              receipt.opencl_coarse_frequency_fallbacks == 0 &&
              std::ranges::all_of(receipt.opencl_coarse_fallback_counts, [](auto count) { return count == 0; }),
              "one-ray coarse FP4 session fell back or kept its device lease");

        const auto& plan = dense.work.plan;
        const auto score_rows = plan.workspaces * plan.frequency_points * plan.arrival_points;
        check(plan.route == r::CoarseRoute::frequency_tiles && plan.workspaces == 78 && plan.rays == 1 &&
              native.work.plan.rays == 1 && plan.frequency_points == native.work.plan.frequency_points &&
              plan.arrival_points == native.work.plan.arrival_points && plan.arrival_points >= 16,
              "one-ray coarse FP4 session changed the public search geometry");
        const auto& bulk = *dense.work.bulk;
        check(bulk.executed && bulk.score_rows == score_rows && bulk.opencl_coarse_score_rows == score_rows &&
              dense.work.search.operations.score_rows == score_rows &&
              bulk.opencl_coarse_issued_products >= bulk.opencl_coarse_useful_products &&
              bulk.opencl_coarse_useful_products == score_rows * 4 * plan.projected_template_rows,
              "one-ray coarse FP4 session did not credit every public score row");
        if (variant.synthesize)
            check(bulk.opencl_coarse_synthesized_columns == plan.workspaces && bulk.opencl_coarse_uploaded_columns == 0 &&
                  dense.work.template_bank.operations.projected_columns == plan.workspaces &&
                  dense.work.template_bank.operations.source_render_rows == 0 &&
                  host->peak < plan.workspaces * plan.raw_template_rows * sizeof(std::complex<float>),
                  "one-ray coarse FP4 session did not render its bank on the device");
        else
            check(bulk.opencl_coarse_synthesized_columns == 0 && bulk.opencl_coarse_uploaded_columns == plan.workspaces,
                  "one-ray coarse FP4 session did not score its uploaded bank");
        const auto rescored = bulk.opencl_coarse_rescored_peaks;
        check(plan.precision_reason == variant.reason && rescored > 0 &&
              bulk.opencl_coarse_rescore_fp32_partial_peaks == (variant.rescore == fp32_partial ? rescored : 0) &&
              bulk.opencl_coarse_rescore_fp64_peaks == (variant.rescore == fp64 ? rescored : 0),
              "one-ray coarse FP4 session reported another arithmetic route");

        const auto& expected = winner(native);
        const auto& actual = winner(dense);
        check(expected.bit == f.bit && expected.coordinate.epoch_offset_seconds == f.epoch,
              "native one-ray coarse fixture lost its planted alternative");
        check(actual.bit == expected.bit && actual.coordinate == expected.coordinate && actual.rank == expected.rank,
              "one-ray coarse FP4 winner differs from the native search");
        check(std::abs(actual.score - expected.score) <= 2e-4 * expected.score,
              "one-ray coarse FP4 rescored winner differs from the native FP64 score");
        check(dense.coordinates.size() == native.coordinates.size() && dense.proposal_count == native.proposal_count,
              "one-ray coarse FP4 session changed the number of refined coordinates");
        std::cout << "OpenCL coarse session, one ray, " << variant.name << ", noise " << noise_sigma
                  << ": winner score " << actual.score << " (native " << expected.score << "), " << score_rows
                  << " score rows, " << plan.arrival_points << " arrivals at " << plan.projected_sample_rate_hz
                  << " Hz, " << bulk.opencl_coarse_issued_products << " issued and "
                  << bulk.opencl_coarse_useful_products << " useful products, lease " << host->peak << " bytes\n";
    }
}

// Through the receiver: a condition that withholds synthesis, or automatic
// options without one-ray admission, hand a one-ray session to the FFT
// session with its native columns, and the receipt names that one reason.
// Midpoint gain timing implies a varying passband and is named first.
void check_one_ray_declines(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& crypto) {
    const std::array<r::FrequencyInterval, 1> prior{{{.03125, .045}}};
    constexpr unsigned workers = 3;
    struct Variant {
        std::string_view reason;
        bool varying, one_ray;
        r::MidpointTimingEvaluation midpoint;
    };
    using enum r::MidpointTimingEvaluation;
    for (const auto& variant : {Variant{"one-ray-unqualified", false, false, off},
                                Variant{"varying-passband", true, true, off},
                                Variant{"midpoint-gain", true, true, short_tracking}}) {
        auto f = capture(crypto, 0, 0, 0);
        f.config.helper_enabled = true;
        f.config.time_varying_channel = variant.varying;
        const auto host = std::make_shared<Host>();
        f.config.bulk_backend = std::make_shared<Backend>(host, device, true, Route::automatic,
                                                          r::OpenClRescoreArithmetic::fp64, variant.one_ray);
        received = {};
        bool delegated = false;
        try {
            (void)r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers,
                                  r::coarse_workspace(f.config, f.slot, workers, {}, variant.midpoint), {}, nullptr,
                                  variant.midpoint);
        } catch (const Delegated&) { delegated = true; }
        const auto receipt = host->receipt();
        check(delegated && received.native_columns && received.taps == 1 && received.arrivals >= 16 &&
              host->peak == 0 && only_fallback(receipt, variant.reason) && receipt.opencl_coarse_dispatches == 0,
              "a declined one-ray session did not keep its route with the receiver's named reason");
        std::cout << "OpenCL coarse session, one ray, " << received.arrivals << " arrivals: fallback named "
                  << variant.reason << '\n';
    }
}

// A one-ray window with fewer arrivals than a block's sixteen-row tile keeps
// its existing route, and names why.
void check_few_arrivals(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& crypto) {
    auto f = capture(crypto, 0, 0, 0);
    f.slot.arrival_lower_seconds = -.02;
    f.slot.arrival_upper_seconds = .02;
    const std::array<r::FrequencyInterval, 1> prior{{{.03125, .045}}};
    f.config.helper_enabled = true;
    constexpr unsigned workers = 3;
    const auto host = std::make_shared<Host>();
    f.config.bulk_backend = std::make_shared<Backend>(host, device, true, Route::automatic);
    received = {};
    bool delegated = false;
    try {
        (void)r::score_coarse(f.config, f.slot, crypto, f.iq, prior, workers,
                              r::coarse_workspace(f.config, f.slot, workers), {});
    } catch (const Delegated&) { delegated = true; }
    const auto receipt = host->receipt();
    check(delegated && received.native_columns && received.taps == 1 && received.arrivals < 16 &&
          host->peak == 0 && only_fallback(receipt, "few-arrivals") && receipt.opencl_coarse_dispatches == 0,
          "a one-ray window of few arrivals did not keep its route with a named reason");
    std::cout << "OpenCL coarse session, one ray, " << received.arrivals
              << " arrivals: fallback named few-arrivals\n";
}

// The named reasons of sessions that never score, through the adapter alone.
void check_fallback_reasons(const std::shared_ptr<r::OpenClNvfp4Device>& device) {
    constexpr std::size_t rows = 64, lags = 40, workspaces = 3;
    const r::CoarseBulkGeometry geometry{1, rows, 128, lags, 2, 240, .5, 2};
    const std::vector<v::Complex> column(rows, {1, 0}), observation(rows + lags - 1, {.5, 0});
    const std::vector<r::CoarseBulkWorkspace> bank(workspaces, {{column, {}}, 1. / rows, 0, {}});
    const std::vector<std::complex<float>> chips(3);
    const std::vector<float> pulse(8 * 256 + 1);
    const std::vector<double> taps(9, 1. / 9);
    const std::vector<r::CoarseTemplateColumn> maps(workspaces);
    r::CoarseTemplateSource source;
    source.lanes = source.cells = 1;
    source.source_rows = 240;
    source.raw_rows = 256;
    source.decimation = 4;
    source.chips = chips;
    source.pulse = pulse;
    source.taps = taps;
    source.columns = maps;
    const r::OpenClCoarseOptions automatic{false, false, true, false, r::OpenClRescoreArithmetic::fp64, true};
    auto unqualified = automatic;
    unqualified.one_ray = false;
    const auto run = [&](std::string_view reason, std::size_t limit, auto&& before,
                         const r::CoarseBulkGeometry& prepared, std::span<const r::CoarseZoomGroup> groups = {},
                         const r::OpenClCoarseOptions* options = nullptr, bool exhausted = false) {
        const auto host = std::make_shared<Host>();
        host->limit = limit;
        host->exhausted = exhausted;
        auto session = r::make_opencl_coarse_bulk(host, device, std::make_unique<FallbackProbe>(),
                                                  options ? *options : automatic);
        before(*session);
        r::CoarseBulkWork work;
        received = {};
        bool delegated = false;
        try { session->prepare(prepared, observation, bank, groups, work, {}); } catch (const Delegated&) { delegated = true; }
        check(delegated && received.native_columns && host->peak == 0 && only_fallback(host->receipt(), reason),
              "a declined dense coarse session miscounted its fallback reason");
    };
    const auto query = [&](const r::CoarseBulkGeometry& offered) {
        return [&, offered](r::CoarseBulk& session) {
            check(!session.supports_template_synthesis(offered, workspaces, source),
                  "a dense coarse session admitted a geometry it must decline");
        };
    };
    const auto none = [](r::CoarseBulk&) {};
    run("unoffered", std::size_t{1} << 30, none, geometry);
    run("mapped-profile", std::size_t{1} << 30, [](r::CoarseBulk& session) {
        session.withhold_template_synthesis(r::CoarseSynthesisWithheld::mapped_profile); }, geometry);
    run("varying-passband", std::size_t{1} << 30, [](r::CoarseBulk& session) {
        session.withhold_template_synthesis(r::CoarseSynthesisWithheld::varying_passband); }, geometry);
    auto few = geometry;
    few.lag_count = 4;
    run("few-arrivals", std::size_t{1} << 30, query(few), few);
    run("workspace-lease", 4096, query(geometry), geometry);
    auto three = geometry;
    three.taps = 3;
    run("kernel-extent", std::size_t{1} << 30, query(three), geometry);
    // Without one-ray admission an automatic session keeps its route, and
    // with it a one-ray plan above the measured dense work per workspace and
    // frequency does, before anything is leased.
    run("one-ray-unqualified", std::size_t{1} << 30, query(geometry), geometry, {}, &unqualified);
    auto costly = geometry;
    costly.lag_count = 4'800'000;
    run("dense-cost", std::size_t{1} << 30, query(costly), geometry);
    auto measured = geometry;
    measured.lag_count = 4'700'000;
    run("workspace-lease", 4096, query(measured), geometry);
    run("host-allocation", std::size_t{1} << 30, query(geometry), geometry, {}, nullptr, true);
    const std::array<v::Complex, 1> phase{};
    const std::array<r::CoarseZoomGroup, 1> zoom{{{0, 1, phase, {}}}};
    run("lag-group-route", std::size_t{1} << 30, none, geometry, zoom);
    // The query admits this one-ray geometry within its lease.
    {
        const auto host = std::make_shared<Host>();
        const auto session = r::make_opencl_coarse_bulk(host, device, std::make_unique<FallbackProbe>(), automatic);
        check(session->supports_template_synthesis(geometry, workspaces, source) && host->peak == 0,
              "a dense coarse session declined a one-ray geometry");
    }
    // A required session reports what an automatic one counts.
    {
        const auto host = std::make_shared<Host>();
        auto session = r::make_opencl_coarse_bulk(host, device, {}, {true, false, true, true});
        r::CoarseBulkWork work;
        std::string reason;
        try { session->prepare(geometry, observation, bank, zoom, work, {}); }
        catch (const v::ReceiverWorkspaceError& error) { reason = error.what(); }
        check(reason.find("no route") != std::string::npos && host->receipt().opencl_coarse_frequency_fallbacks == 0,
              "a required dense coarse session counted or accepted a lag-group route");
    }
    std::cout << "OpenCL coarse sessions name their fallback reasons\n";
}

} // namespace

int main(int argc, char** argv) {
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
        if (argc > 1 && std::string_view(argv[1]) == "one-ray") {
            check_fallback_reasons(device);
            check_few_arrivals(device, crypto);
            check_one_ray_declines(device, crypto);
            check_one_ray_sessions(device, crypto, .12);
            return 0;
        }
        check_session(device, crypto, 0, false);
        check_session(device, crypto, .12, true);
        check_session(device, crypto, 0, false, true);
        check_conditioning(device, crypto);
        check_segmented_forwarding(device);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
