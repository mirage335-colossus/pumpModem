// The production fine session through the resident OpenCL route. Dense tiles
// that the device renders, and windows of several dispatches, must leave the
// receiver state of the route that uploads native tiles one dispatch at a
// time. On the kernel emulation device this checks orchestration and kernel
// logic. It is a device result only when built for the device.
#include "../src/triad_opencl_fine_bulk.hpp"
#include "../src/triad_opencl_nvfp4_projection.hpp"
#include "../src/triad_receiver_fine.hpp"
#include "datapump/host/sandbox.hpp"
#include "triad_crypto_fixture.hpp"
#include "triad_opencl_nvfp4_ptx.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>

namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class Host final : public r::OpenClFineHost {
public:
    explicit Host(std::size_t device_limit) : limit(device_limit) {}
    bool try_lease(std::size_t bytes) override {
        std::lock_guard lock(mutex_);
        if (bytes > limit - leased_) return false;
        // A lease above this size is refused this many times, as when other
        // owners hold the budget for a while.
        if (bytes > refuse_above && refusals) {
            --refusals;
            return false;
        }
        leased_ += bytes;
        peak = std::max(peak, leased_);
        return true;
    }
    void release(std::size_t bytes) noexcept override {
        std::lock_guard lock(mutex_);
        leased_ -= bytes;
    }
    void account(const r::BulkReceipt& delta) override {
        std::lock_guard lock(mutex_);
        r::add_opencl_fine_work(total_, delta);
    }
    r::BulkReceipt receipt() const override {
        std::lock_guard lock(mutex_);
        return total_;
    }
    std::size_t leased() const {
        std::lock_guard lock(mutex_);
        return leased_;
    }

    std::size_t limit, peak = 0, refuse_above = 0, refusals = 0;

private:
    mutable std::mutex mutex_;
    r::BulkReceipt total_;
    std::size_t leased_ = 0;
};

// The fine half of the Vulkan backend's public contract.
class Backend final : public r::ReceiverBulkBackend {
public:
    Backend(std::shared_ptr<Host> host, std::shared_ptr<const r::OpenClNvfp4Device> device,
            const r::OpenClFineOptions& options)
        : host_(std::move(host)), device_(std::move(device)), options_(options) {}
    bool supports_fine() const noexcept override { return false; }
    bool fine_maps_on_host() const noexcept override { return true; }
    std::string_view fine_fallback_reason(const r::FineBulkGeometry& geometry) const noexcept override {
        return r::opencl_fine_supports(geometry) ? std::string_view{} : "native-fine-backend";
    }
    std::size_t fine_resident_blocks(const r::FineBulkGeometry&) const noexcept override {
        return r::opencl_fine_resident_blocks(options_);
    }
    std::unique_ptr<r::FineBulk> make_fine(double, double) override {
        return r::make_opencl_fine_bulk(host_, device_, options_);
    }
    std::unique_ptr<r::CoarseBulk> make_coarse(double, double) override { return {}; }
    std::size_t fine_host_bytes(std::size_t, std::size_t) const override { return r::opencl_fine_host_bytes(options_); }
    std::size_t coarse_host_bytes(std::size_t, std::size_t, std::size_t) const override { return 0; }
    r::BulkReceipt receipt() const override { return host_->receipt(); }

private:
    std::shared_ptr<Host> host_;
    std::shared_ptr<const r::OpenClNvfp4Device> device_;
    r::OpenClFineOptions options_;
};

struct Fixture {
    v::ReceiverConfig config;
    v::ReceiverSlotPlan slot;
    std::vector<v::Complex> iq;
    // The wanted epoch and a neighbouring one, for two keyed lane pairs.
    std::array<r::FineCoordinate, 2> coordinates{{{0, 0, 0, 0}, {1, .003, 0, 0}}};
};

// Four segments of the isolated 1500-row, 47-mode, two-path DATA geometry and
// a terminal segment that its remainder enlarges: nine resident blocks and
// one native one. The source runs below the receive rate.
Fixture capture(const datapump::Crypto& key) {
    constexpr std::size_t fs = 6000, rows = 5 * 3000 + 321;
    Fixture f;
    auto& c = f.config;
    auto& s = f.slot;
    c.sample_rate_hz = fs;
    c.data_if_hz = 1725;
    c.noise_variance = .05;
    c.clock_bound_ppm = 40;
    c.time_varying_channel = true;
    c.spread_delay_seconds = .021;
    c.spread_doppler_hz = 80;
    s.index = 5;
    s.first_sample = 90'000'000'000ULL;
    s.stop_sample = s.first_sample + rows;
    s.quiet_first_sample = s.first_sample + 1500;
    s.quiet_stop_sample = s.stop_sample - 1821;
    s.format.sample_rate = 2400;
    s.format.inner_rate_hz = 36;
    s.format.added_symbols = 1;
    s.format.bit_seconds = .001;
    s.format.pulse_shape = v::PulseShape::TaperedRrc;
    s.format.epoch = 1793850000;
    s.format.symbol_ordinal = 3117;
    // The window opens four seconds into the source.
    s.window_capture_lead_seconds = -17;
    s.source_start_seconds = 13;
    v::PatternSource source(s.format, key, 1);
    check(source.geometry().source_samples > (rows + 4 * fs) * 2 / 5 + 2400,
          "fine session fixture does not cover its window with DATA");
    f.iq.resize(rows);
    std::mt19937 random(90125);
    std::normal_distribution<double> noise(0, .05);
    for (std::size_t i = 0; i != rows; ++i)
        f.iq[i] = v::Complex(.8, .3) * source.sample_at(static_cast<long double>(i + 4 * fs) * .4L) +
                  v::Complex(noise(random), noise(random));
    return f;
}

struct Outcome {
    v::ReceiverSlotScore score;
    r::FineProgress work;
    r::BulkReceipt receipt;
    std::size_t peak_lease = 0;
};

// Work of another owner of the backend, which the session receipt leaves out.
r::BulkReceipt foreign_work() {
    r::BulkReceipt work;
    work.upload_bytes = 1234567;
    work.synchronizations = 89;
    work.opencl_resident_fine_batches = 3;
    work.opencl_resident_fine_blocks = 40;
    work.opencl_resident_window_blocks = 16;
    return work;
}

Outcome run(const Fixture& f, const datapump::Crypto& key, const std::shared_ptr<r::OpenClNvfp4Device>& device,
            const std::optional<r::OpenClFineOptions>& options, r::FineObservation extent,
            std::size_t checkpoint = 0, std::size_t device_limit = std::size_t{1} << 30,
            std::size_t refuse_above = 0, std::size_t refusals = 0, bool foreign = false) {
    constexpr unsigned workers = 3;
    auto config = f.config;
    const auto host = std::make_shared<Host>(device_limit);
    host->refuse_above = refuse_above;
    host->refusals = refusals;
    if (foreign) host->account(foreign_work());
    if (options) config.bulk_backend = std::make_shared<Backend>(host, device, *options);
    Outcome outcome;
    {
        const auto first = static_cast<std::size_t>(extent.first_sample - f.slot.first_sample);
        r::FineSession session(config, f.slot, key, extent, std::span(f.iq).subspan(first, extent.rows), {},
                               f.coordinates, 100, 0, workers, r::fine_workspace(config, f.slot, extent, workers));
        if (checkpoint) session.advance_to(checkpoint);
        session.advance_to(extent.rows);
        session.finish(outcome.score);
        outcome.work = session.progress();
    }
    check(host->leased() == 0, "a fine session kept its device lease");
    outcome.receipt = host->receipt();
    if (foreign) {
        // The session receipt is the session's own work, the backend's the total.
        const auto expected = foreign_work();
        const auto& own = *outcome.work.bulk;
        check(own.upload_bytes + expected.upload_bytes == outcome.receipt.upload_bytes &&
              own.synchronizations + expected.synchronizations == outcome.receipt.synchronizations &&
              own.opencl_resident_fine_blocks + expected.opencl_resident_fine_blocks ==
                  outcome.receipt.opencl_resident_fine_blocks &&
              own.opencl_resident_window_blocks < expected.opencl_resident_window_blocks &&
              outcome.receipt.opencl_resident_window_blocks == expected.opencl_resident_window_blocks,
              "a fine session receipt included another owner's work");
        auto own_total = outcome.receipt;
        own_total.upload_bytes -= expected.upload_bytes;
        own_total.synchronizations -= expected.synchronizations;
        own_total.opencl_resident_fine_batches -= expected.opencl_resident_fine_batches;
        own_total.opencl_resident_fine_blocks -= expected.opencl_resident_fine_blocks;
        own_total.opencl_resident_window_blocks = own.opencl_resident_window_blocks;
        outcome.receipt = own_total;
    }
    outcome.peak_lease = host->peak;
    return outcome;
}

double relative(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    double worst = 0;
    for (std::size_t bit = 0; bit != 2; ++bit)
        worst = std::max(worst, std::abs(a[bit] - b[bit]) / std::max(1., std::abs(b[bit])));
    return worst;
}

void same_public_work(const Outcome& a, const Outcome& b, const char* message) {
    const auto& x = a.work.initial_affine;
    const auto& y = b.work.initial_affine;
    check(a.score.quiet_energy == b.score.quiet_energy && a.score.quiet_modes == b.score.quiet_modes &&
          a.score.complete == b.score.complete && a.score.basis_rows == b.score.basis_rows &&
          a.score.affine_blocks == b.score.affine_blocks && a.work.blocks == b.work.blocks &&
          a.work.segments == b.work.segments && x.retained_states == y.retained_states &&
          x.basis_rows == y.basis_rows && x.projection_modes == y.projection_modes &&
          x.gaussian_updates == y.gaussian_updates, message);
}

void check_slot(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& key) {
    const auto f = capture(key);
    const r::FineObservation whole{f.slot.first_sample, f.iq.size(), r::FineRunKind::operational, true};
    constexpr std::size_t states = 6, resident_blocks = 9, blocks = 10, modes = 47;

    const auto native = run(f, key, device, std::nullopt, whole);
    check(native.score.complete && native.work.blocks == blocks && native.work.last_block_rows == 1821 &&
          native.work.initial_affine.initial_states == states &&
          native.score.bit_evidence[1] > native.score.bit_evidence[0] + 100,
          "native fine fixture lost its geometry or its planted bit");

    r::OpenClFineOptions options;
    options.device_tiles = false;
    options.window_blocks = 2;
    // The route before device tiles and windows: native tiles, one dispatch a batch.
    const auto uploaded = run(f, key, device, options, whole);
    options.device_tiles = true;
    const auto rendered = run(f, key, device, options, whole);
    options.window_blocks = 6;
    const auto window = run(f, key, device, options, whole, 0, std::size_t{1} << 30, 0, 0, true);
    options.window_blocks = r::opencl_fine_default_window_blocks;
    options.profile = true;
    const auto wide = run(f, key, device, options, whole);
    options.profile = false;
    // Staging the whole window before the device starts is the control of
    // the streamed mappings.
    options.stream_blocks = 0;
    const auto unstreamed = run(f, key, device, options, whole);
    options.stream_blocks = r::opencl_fine_default_stream_blocks;

    for (const auto* route : {&uploaded, &rendered, &window, &wide}) {
        same_public_work(*route, native, "a resident route changed the public fine work or quiet statistics");
        const auto& receipt = route->receipt;
        check(route->score.complete && route->work.bulk && receipt.executed &&
              receipt.opencl_resident_reduced_windows == 0 &&
              receipt.statistics_blocks == blocks * states &&
              receipt.projected_values == native.work.initial_affine.projection_modes &&
              receipt.opencl_resident_fine_blocks == resident_blocks && receipt.opencl_nvfp4_native_blocks == 1 &&
              receipt.opencl_resident_nvfp4_modes == resident_blocks * states * 4 * modes &&
              receipt.opencl_resident_download_bytes == resident_blocks * states * 11 * 16 &&
              receipt.opencl_resident_direct_columns == 0 &&
              receipt.mapped_columns == resident_blocks * states * 4,
              "a resident route did not credit every public block once");
    }

    // Rendered tiles replace every uploaded one and leave the evidence.
    const auto tile_difference = relative(rendered.score.bit_evidence, uploaded.score.bit_evidence);
    check(tile_difference < 1e-6, "device-rendered tiles changed the fixed-affine evidence");
    check(uploaded.receipt.uploaded_tiles > 0 && uploaded.receipt.opencl_resident_rendered_tiles == 0 &&
          uploaded.work.cache.native_preparation_calls > 0 && uploaded.work.cache.addressed_columns == 0,
          "the uploaded-tile route did not render its tiles natively");
    // Only the native terminal block still fills a tile of each keyed epoch.
    for (const auto* route : {&rendered, &window, &wide})
        check(route->receipt.uploaded_tiles == 0 &&
              route->receipt.opencl_resident_rendered_tiles == uploaded.receipt.uploaded_tiles &&
              route->receipt.opencl_resident_keyed_lanes == 4 && route->work.cache.native_preparation_calls == 2 &&
              uploaded.work.cache.native_preparation_calls > 2 &&
              route->work.cache.addressed_columns == resident_blocks * states * 4 &&
              route->work.cache.keyed_lanes == 4 &&
              4 * route->receipt.opencl_resident_upload_bytes < uploaded.receipt.opencl_resident_upload_bytes,
              "a rendering route still uploaded or natively rendered its resident tiles");

    // A window holds the dispatches of several segments. Each lane's device
    // arithmetic is its own, so the evidence does not depend on the batch.
    check(window.score.bit_evidence == rendered.score.bit_evidence &&
          wide.score.bit_evidence == rendered.score.bit_evidence,
          "a resident window changed the fixed-affine evidence");
    check(uploaded.receipt.opencl_resident_fine_batches == 5 && rendered.receipt.opencl_resident_fine_batches == 5 &&
          rendered.receipt.opencl_resident_fine_pairs == 4 && window.receipt.opencl_resident_fine_batches == 2 &&
          wide.receipt.opencl_resident_fine_batches == 1 &&
          wide.receipt.opencl_resident_dispatches < window.receipt.opencl_resident_dispatches &&
          window.receipt.opencl_resident_dispatches < uploaded.receipt.opencl_resident_dispatches &&
          uploaded.receipt.opencl_resident_window_blocks == 2 && rendered.receipt.opencl_resident_window_blocks == 2 &&
          window.receipt.opencl_resident_window_blocks == 6 && wide.receipt.opencl_resident_window_blocks == 9,
          "resident windows did not reduce the device round trips");
    // With device tiles only the session's setup blocks here: one write of
    // the DFT, two of the source and one per keyed lane at its first use.
    // Exact native columns, which this slot has none of, would block too. A
    // window submits the mappings of every four blocks while the owner
    // stages the next ones.
    for (const auto* route : {&rendered, &window, &wide})
        check(route->receipt.opencl_resident_blocking_writes == 3 + 4 && route->receipt.opencl_resident_async_writes > 0,
              "a rendering route waited on a write beyond its setup");
    check(rendered.receipt.opencl_resident_mapping_dispatches == 5 &&
          window.receipt.opencl_resident_mapping_dispatches == 3 && wide.receipt.opencl_resident_mapping_dispatches == 3,
          "a resident window did not submit its mappings every four blocks");
    check(unstreamed.score.bit_evidence == wide.score.bit_evidence &&
          unstreamed.receipt.opencl_resident_mapping_dispatches == 1 &&
          unstreamed.receipt.opencl_resident_fine_batches == 1 &&
          unstreamed.receipt.opencl_resident_upload_bytes == wide.receipt.opencl_resident_upload_bytes,
          "streamed mappings changed a window's evidence or uploads");
    // A window holds whole dispatches.
    r::OpenClFineOptions odd = options;
    odd.window_blocks = 7;
    check(r::opencl_fine_resident_blocks(odd) == 6, "an odd resident window was not reduced to whole dispatches");
    check(wide.peak_lease > rendered.peak_lease && wide.peak_lease < (std::size_t{128} << 20),
          "a resident window did not lease its own bounded workspace");
    const auto& profile = wide.receipt;
    check(profile.opencl_resident_host_source_seconds > 0 && profile.opencl_resident_host_staging_seconds > 0 &&
          profile.opencl_resident_host_transfer_seconds > 0 && profile.opencl_resident_host_wait_seconds > 0 &&
          profile.opencl_resident_device_render_seconds > 0 && profile.opencl_resident_device_map_seconds > 0 &&
          profile.opencl_resident_device_pack_seconds > 0 && profile.opencl_resident_device_product_seconds > 0 &&
          profile.opencl_resident_device_statistics_seconds > 0 &&
          profile.opencl_resident_device_span_seconds >= profile.opencl_resident_device_product_seconds &&
          rendered.receipt.opencl_resident_device_span_seconds == 0,
          "a profiled resident session did not report its host spans and device intervals");

    // FP4 quantizes the templates. The planted bit and its margin remain.
    const auto fp4_difference = relative(wide.score.bit_evidence, native.score.bit_evidence);
    check(wide.score.bit_evidence[1] > wide.score.bit_evidence[0] + 100 && fp4_difference < .05,
          "resident FP4 evidence left the native fixed-affine evidence");

    // A lease too small for the window still holds one dispatch a batch,
    // also when the session resumes inside a window.
    const auto pair_bytes = rendered.peak_lease;
    const auto tight = run(f, key, device, options, whole, 3000 + 1500, pair_bytes);
    check(tight.score.bit_evidence == rendered.score.bit_evidence && tight.peak_lease == pair_bytes &&
          tight.receipt.opencl_resident_fine_batches == 5 && tight.receipt.opencl_resident_fine_blocks == resident_blocks &&
          tight.receipt.opencl_resident_window_blocks == 2 && tight.receipt.opencl_resident_reduced_windows > 0,
          "a tight device lease did not fall back to single-dispatch batches");
    // A window lease refused once runs that window in pairs, and the next
    // window asks again and gets it while still holding the pair session.
    options.window_blocks = 6;
    const auto transient = run(f, key, device, options, whole, 0, std::size_t{1} << 30, pair_bytes, 1);
    check(transient.score.bit_evidence == rendered.score.bit_evidence &&
          transient.receipt.opencl_resident_reduced_windows == 1 &&
          transient.receipt.opencl_resident_window_blocks == 3 &&
          transient.receipt.opencl_resident_fine_blocks == resident_blocks &&
          transient.peak_lease > window.peak_lease,
          "a window lease refused once did not return for the next window");
    options.window_blocks = r::opencl_fine_default_window_blocks;
    // With no lease at all, every block of every window takes the native route.
    const auto declined = run(f, key, device, options, whole, 0, 4096);
    same_public_work(declined, native, "a declined resident window changed the public fine work");
    check(declined.receipt.opencl_resident_fine_batches == 0 && declined.receipt.opencl_nvfp4_native_blocks == blocks &&
          declined.receipt.native_fine_fallback_reason == "opencl-resident-workspace-limit" &&
          declined.receipt.statistics_blocks == blocks * states && declined.peak_lease == 0 &&
          relative(declined.score.bit_evidence, native.score.bit_evidence) < 1e-5,
          "a declined resident window did not complete on the native route");

    std::cout << "OpenCL fine session: " << blocks << " blocks, " << states << " states. Batches "
              << uploaded.receipt.opencl_resident_fine_batches << " -> " << wide.receipt.opencl_resident_fine_batches
              << ", blocking writes " << uploaded.receipt.opencl_resident_blocking_writes << " -> "
              << wide.receipt.opencl_resident_blocking_writes << " (setup), writes without a wait "
              << uploaded.receipt.opencl_resident_async_writes << " -> " << wide.receipt.opencl_resident_async_writes
              << ", upload bytes "
              << uploaded.receipt.opencl_resident_upload_bytes << " -> " << wide.receipt.opencl_resident_upload_bytes
              << ", device lease " << rendered.peak_lease << " -> " << wide.peak_lease
              << ". Evidence change from rendered tiles " << tile_difference << ", FP4 against native "
              << fp4_difference << '\n';
}

// The adaptive lane's maps follow its pruning, so it keeps one dispatch a
// batch, and leases no more than that. Its tiles are still addressed and
// rendered on the device.
void check_adaptive(const std::shared_ptr<r::OpenClNvfp4Device>& device, const datapump::Crypto& key) {
    const auto f = capture(key);
    r::FineObservation crop{f.slot.first_sample + 3000, 2 * 3000, r::FineRunKind::probe, true};
    crop.lanes = r::FineLanes::fixed_and_adaptive;
    r::OpenClFineOptions options;
    options.device_tiles = false;
    const auto uploaded = run(f, key, device, options, crop);
    options.device_tiles = true;
    const auto rendered = run(f, key, device, options, crop);
    options.window_blocks = 2;
    const auto pairs = run(f, key, device, options, crop);
    check(rendered.peak_lease == pairs.peak_lease && rendered.receipt.opencl_resident_window_blocks == 2,
          "the adaptive lane leased a window it does not submit");
    check(uploaded.work.adaptive.gaussian_updates > 0 && rendered.work.blocks == 4 &&
          rendered.receipt.opencl_resident_fine_batches == 2 && rendered.receipt.opencl_resident_fine_pairs == 2 &&
          uploaded.receipt.opencl_resident_fine_batches == 2 && rendered.receipt.uploaded_tiles == 0 &&
          rendered.receipt.opencl_resident_rendered_tiles == uploaded.receipt.uploaded_tiles &&
          rendered.receipt.statistics_blocks == uploaded.receipt.statistics_blocks,
          "the adaptive lane left its single-dispatch resident batches");
    check(relative(rendered.work.initial_affine.bit_scores, uploaded.work.initial_affine.bit_scores) < 1e-6 &&
          relative(rendered.work.adaptive.bit_scores, uploaded.work.adaptive.bit_scores) < 1e-6 &&
          rendered.work.adaptive.retained_states == uploaded.work.adaptive.retained_states,
          "device-rendered tiles changed the adaptive lane");
    std::cout << "OpenCL fine session: adaptive lane keeps " << rendered.receipt.opencl_resident_fine_batches
              << " single-dispatch batches with rendered tiles\n";
}

} // namespace

int main() {
    try {
        constexpr std::array descriptors{0, 1, 2};
        datapump::host::restrict_descriptors(descriptors);
        datapump::host::install_no_socket_boundary();
        std::array<std::uint8_t, 32> seed{};
        for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<std::uint8_t>(37 * i + 5);
        const auto key = triad_test_key(seed);
        const std::array<std::byte, 16> any_uuid{};
        std::string reason;
        const auto device = r::OpenClNvfp4Device::open(any_uuid, std::as_bytes(std::span(r::opencl_nvfp4_ptx)), &reason);
        if (!device) throw std::runtime_error(reason);
        check_slot(device, key);
        check_adaptive(device, key);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
