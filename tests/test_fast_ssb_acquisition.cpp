#include "datapump/fast/modem.hpp"
#include "datapump/fast/simulation.hpp"
#include "fast/ssb_acquisition.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
namespace fast = datapump::fast;
using namespace datapump::fast::detail;
using Complex = std::complex<double>;
using Iq = std::complex<float>;

void require(bool condition, const char* failure) {
    if (!condition) throw std::runtime_error(failure);
}

fast::SimulationSettings channel_settings(std::string_view name) {
    fast::SimulationSettings channel;
    if (name == "clean") {
        channel.noise_dbm_hz.reset();
        channel.seed = 421;
        return channel;
    }
    channel.selective_slow_depth = channel.selective_fast_depth = .65;
    if (name == "hf") {
        channel.transmit_watts = 40.;
        channel.path_loss_db = 130.;
        channel.baseband_ppm = 100.;
        channel.baseband_phase_rad_sqrt_s = .5 * std::numbers::pi / 180.;
        channel.shift_ppm = 1.;
        channel.shift_phase_rad_sqrt_s = .05 * std::numbers::pi / 180.;
        channel.shift_hz = 30e6;
        channel.delay_ms = 7.;
        channel.doppler_hz = 30.;
        channel.margin = 3.;
        channel.seed = 417;
        return channel;
    }
    if (name != "eme") throw std::invalid_argument("Unknown Fast SSB acquisition fixture");
    channel.transmit_watts = 150000.;
    channel.path_loss_db = 180.;
    channel.baseband_ppm = channel.shift_ppm = .0001;
    channel.baseband_phase_rad_sqrt_s = channel.shift_phase_rad_sqrt_s = .005 * std::numbers::pi / 180.;
    channel.shift_hz = 5.8e9;
    channel.delay_ms = .5;
    channel.doppler_hz = 6.;
    channel.margin = 1.;
    channel.seed = 419;
    return channel;
}

void run(std::string_view name) {
    const auto p = fast::capacity_profile(fast::Channel::ssb);
    const auto settings = channel_settings(name);
    const auto frontend_geometry = make_ssb_frontend_geometry(p.sample_rate, p.carrier_hz);
    const DelayDopplerFit fitter(p, frontend_geometry);
    const auto fixed_training_start = p.sample_rate * 147 / 1000;
    const auto word = 64. * p.sample_rate / p.symbol_rate;
    const auto stride = frontend_geometry.decimation;
    const auto radius = frontend_geometry.radius();
    // The search is identical across fixtures and covers waveform starts
    // before the observed origin. The source's idle is fixture-only.
    SsbAcquisitionRequest request{-6. * word, -3. * word, radius +
        (fixed_training_start - radius) / stride * stride, 24, 280., .022 * p.sample_rate, .0003};
    const auto timing = fitter.timing(request);
    require(timing.latest_training_start_after_epoch_ticks > timing.earliest_training_start_after_epoch_ticks,
        "Fast SSB training scheduler has no admissible public interior");

    std::uint64_t state = 0x836a743ffbe71f9dULL;
    fast::Transmitter transmitter(p, [&](std::span<std::uint8_t> bits) {
        for (auto& bit : bits) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            bit = state & 1U;
        }
        return true;
    });
    const auto source_start = p.sample_rate * (name == "clean" ? 36 : name == "hf" ? 17 : 23) / 1000;
    auto idle = std::size_t(source_start);
    fast::SimulationChannel channel(p, settings, [&](std::span<Iq> output) {
        const auto leading = std::min(idle, output.size());
        std::fill_n(output.begin(), leading, Iq{});
        idle -= leading;
        return leading + transmitter.read_analytic(output.subspan(leading));
    });
    std::vector<Complex> centers;
    SsbFrontend frontend(frontend_geometry, [&](const SsbFrontendOutput& output) {
        require(output.first_center_input_sample == radius + centers.size() * stride,
            "Acquisition fixture changed its frontend centers");
        centers.insert(centers.end(), output.samples.begin(), output.samples.end());
    });
    std::vector<float> block(1024);
    const auto prefix = p.sample_rate * 110 / 100;
    for (std::size_t done = 0; done < prefix;) {
        const auto input = std::span(block).first(std::min(block.size(), prefix - done));
        const auto got = channel.read(input);
        require(got && frontend.push(input.first(got)) == got, "Fast SSB acquisition prefix truncated");
        done += got;
    }
    SsbAcquisitionView view{radius, stride, centers};
    std::size_t local_origin = 0;
    if (name == "clean") {
        // Capture begins partway into the transmitted ramp. Keep actual
        // center spacing and give the fitter no sample before this origin.
        local_origin = p.sample_rate / 20;
        const auto skip = local_origin / stride;
        view = {radius + skip * stride - local_origin, stride, view.samples.subspan(skip)};
        request.training_first_center_tick -= local_origin;
        // Almost a complete preamble with no fully observed distinct marker.
        auto truncated = view;
        truncated.samples = view.samples.first(p.sample_rate * 99 / (100 * stride) - skip);
        SsbAcquisitionDiagnostics incomplete;
        require(!fitter.fit(truncated, request, {}, &incomplete), "Fast SSB admitted an unobserved marker");
        require(incomplete.status == SsbAcquisitionDiagnostics::Status::no_verified_marker,
            "Fast SSB prefix control never reached independent marker verification");
    }
    SsbAcquisitionDiagnostics receipt;
    const auto lock = fitter.fit(view, request, {}, &receipt);
    std::cout << name << " status=" << int(receipt.status) << " atoms=" << receipt.atom_count
              << " train_nmse=" << receipt.training_nmse << " holdout_nmse=" << receipt.public_holdout_nmse
              << " marker_nmse=" << receipt.marker_nmse << " signed_coherence=" << receipt.marker_signed_coherence
              << " decoy_ratio=" << receipt.marker_decoy_nmse_ratio << " epochs=" << receipt.epoch_candidates
              << " markers=" << receipt.marker_evaluations << " workspace_bound_bytes=" << receipt.workspace_bound_bytes
              << " recent_attempted=" << receipt.recent_refinement_attempted << " recent_used=" << receipt.recent_refinement_used
              << " recent_control_holdout=" << receipt.recent_refinement_control_holdout_nmse
              << " recent_holdout=" << receipt.recent_refinement_holdout_nmse
              << " recent_marker_nmse=" << receipt.recent_refinement_marker_nmse
              << " recent_marker_coherence=" << receipt.recent_refinement_marker_signed_coherence
              << " recent_marker_decoy=" << receipt.recent_refinement_marker_decoy_nmse_ratio;
    if (lock) std::cout << " epoch_ticks=" << lock->reference_input_tick +
        lock->epoch_from_reference_ticks / (1. + lock->clock_ratio_error) <<
        " clock_ppm=" << lock->clock_ratio_error * 1e6;
    std::cout << std::endl;
    require(bool(lock), "Fast SSB did not verify the independent native marker");
    require(lock->atoms.size() <= DelayDopplerFit::maximum_atoms, "Fast SSB lost its fixed atom bound");
    const auto recovered = local_origin + lock->reference_input_tick +
        lock->epoch_from_reference_ticks / (1. + lock->clock_ratio_error);
    require(std::abs(recovered - source_start) < word / 16.,
        "Fast SSB marker verification selected a displaced repetition");
}

} // namespace

int main(int argc, char** argv) {
    run(argc == 2 ? argv[1] : "clean");
}
