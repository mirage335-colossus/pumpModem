#include "fast/ssb_local_fir.hpp"
#include "fast/ssb_display_points.hpp"
#include "datapump/fast/modem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
namespace fast = datapump::fast;
using namespace datapump::fast::detail;
using C = std::complex<double>;

void require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

void check_affine_filter() {
    for (const auto rate : {44100U, 48000U, 192000U}) {
        auto profile = fast::capacity_profile(fast::Channel::ssb);
        profile.sample_rate = rate;
        const auto geometry = make_ssb_frontend_geometry(rate, profile.carrier_hz);
        const auto beta = profile.symbol_rate / profile.sample_rate;
        constexpr double clock = 125e-6;
        const SsbPulse pulse(profile, geometry);
        const auto kernel = pulse.make_cache(clock, 0.);
        const RrcPulseTable transmit(profile.rolloff, single_carrier_pulse_radius(profile));
        const auto radius = static_cast<int>(geometry.radius());
        constexpr C gain{.7, -.35};
        const auto slope = C{.18, .33} / double(radius);
        const auto other = C{-.75, .32} / double(radius);
        const auto direct = [&](double displacement, const auto& gain_at_offset) {
            C sum{};
            for (int tap = -radius; tap <= radius; ++tap)
                sum += geometry.taps[tap + radius] * profile.amplitude *
                    transmit(displacement - (1. + clock) * beta * tap) * gain_at_offset(-tap);
            return sum;
        };
        for (const double x : {-19.113, -1.09, .37, 4.201, 38.56}) {
            const auto expected = direct(x, [&](int offset) { return gain + double(offset) * slope; });
            const auto cached = ssb_affine_path(kernel.kernel(x), gain, slope);
            require(std::abs(expected - cached) < 5e-6, "Fast SSB local affine cache lost the FIR tap time");
        }

        // An aperture spanning a knot is a different channel. The cached
        // affine law represents its explicit extension across the aperture.
        constexpr double x = .37;
        const auto extrapolated = ssb_affine_path(kernel.kernel(x), gain, slope);
        const auto exact_piecewise = direct(x, [&](int offset) {
            return gain + double(offset) * (offset < 0 ? slope : other);
        });
        require(std::abs(extrapolated - exact_piecewise) > 1e-4,
            "Fast SSB local affine test did not exercise a crossing gain knot");
    }
}

void check_routing_aperture() {
    for (const auto rate : {44100U, 48000U}) {
        const auto geometry = make_ssb_frontend_geometry(rate, 1500.);
        const auto stride = geometry.decimation, radius = geometry.radius();
        const auto pad = (2 * radius / stride) * stride;
        const auto first = radius + 5000 * stride;
        const std::array scored{SsbCenterRange{first, first + 80 * stride}};
        const auto before = ssb_disjoint_fit_centers(
            {first - pad - 20 * stride, first - stride}, scored, stride, radius);
        const auto after = ssb_disjoint_fit_centers(
            {scored[0].past_last + stride, scored[0].past_last + pad + 20 * stride}, scored, stride, radius);
        require(before && after, "Fast SSB local FIR rejected usable adjacent fit samples");
        require(before->past_last - stride + radius < scored[0].first - radius &&
            after->first - radius > scored[0].past_last - stride + radius,
            "Fast SSB local FIR routing fit and validation share an input FIR tap");
        require(before->past_last + radius >= scored[0].first - radius &&
            after->first - stride - radius <= scored[0].past_last - stride + radius,
            "Fast SSB local FIR routing clipped a usable next center");
        require(!ssb_disjoint_fit_centers({first - pad, first}, scored, stride, radius),
            "Fast SSB local FIR routing admitted a shared FIR endpoint");
    }
}

void check_clock_midpoint() {
    using Scores = std::array<std::optional<double>, ssb_local_clock_coarse_ppm.size()>;
    const Scores measured{.48332179741228731, .30865095658100383, .16247424071157046,
        .016887259534098663, .015205807574635728};
    require(ssb_local_clock_midpoint(measured, 4) == 37.5, "Fast SSB FIR truncated a positive clock midpoint");
    require(ssb_local_clock_midpoint(Scores{.3, .2, .1, .4, .5}, 2) == -12.5,
        "Fast SSB FIR lost the sign of a negative clock midpoint");
    require(!ssb_local_clock_midpoint(Scores{.1, .3, .2, .4, .5}, 0),
        "Fast SSB FIR interpolated nonadjacent clock candidates");
    require(!ssb_local_clock_midpoint(Scores{std::nullopt, std::nullopt, std::nullopt, .1, std::nullopt}, 3),
        "Fast SSB FIR interpolated an unevaluated clock candidate");
    require(!ssb_local_clock_midpoint(Scores{.2, std::nullopt, std::nullopt, std::nullopt,
        std::numeric_limits<double>::infinity()}, 4),
        "Fast SSB FIR treated a nonfinite clock candidate as valid");
    require(!ssb_local_clock_midpoint(measured, measured.size()), "Fast SSB FIR interpolated an invalid clock index");

    require(ssb_local_clock_holdout_accepts(.98560088912198884, .041495317763093328, .018582986358701099),
        "Fast SSB FIR rejected the validated fractional clock candidate");
    require(!ssb_local_clock_holdout_accepts(.98560088912198884, .041495317763093328, .0392134000548),
        "Fast SSB FIR lost the independent carried-grid comparison");
    require(!ssb_local_clock_holdout_accepts(.02, 1., .019),
        "Fast SSB FIR lost the independent nominal-grid comparison");
    require(!ssb_local_clock_holdout_accepts(1., 1., std::nextafter(.10, 1.)),
        "Fast SSB FIR lost the absolute clock holdout limit");
}

void check_recurring_observation_phase() {
    auto profile = fast::capacity_profile(fast::Channel::ssb);
    profile.ldpc_frame_bits = 16200;
    const auto frontend = make_ssb_frontend_geometry(profile.sample_rate, profile.carrier_hz);
    const auto beta = profile.symbol_rate / profile.sample_rate;
    const auto pulse = SsbPulse(profile, frontend).make_cache(0., 0.);
    const RrcPulseTable tx(profile.rolloff, single_carrier_pulse_radius(profile));
    const std::array gains{C{.8, .2}, C{-.7, .3}, C{.15, -.2}};
    const std::array slopes{C{.0004, .0001}, C{-.0002, .0003}, C{0., -.0002}};
    constexpr auto first_delay = -8.;
    const auto center = ssb_local_coherent_delay(pulse, gains, slopes, first_delay, beta);
    const auto radius = static_cast<int>(frontend.radius());
    const auto first = static_cast<int>(std::floor(-8. * pulse.support_symbols()));
    const auto end = static_cast<int>(std::ceil(8. * (pulse.support_symbols() + 1.)));
    double energy = 0., moment = 0.;
    for (auto i = first; i <= end; ++i) {
        const auto symbol = i / 8.;
        C coherent{};
        for (std::size_t tap = 0; tap < gains.size(); ++tap) {
            for (int k = -radius; k <= radius; ++k)
                coherent += frontend.taps[k + radius] * profile.amplitude *
                    tx(symbol - .5 * tap - beta * k) * (gains[tap] - double(k) * slopes[tap]);
        }
        energy += std::norm(coherent);
        moment += symbol * std::norm(coherent);
    }
    require(center && std::abs(*center - (first_delay + moment / (energy * beta))) < 3e-3,
        "Fast SSB FIR phase center differs from the full coherent native FIR impulse");
    const std::array<C, 1> path{C{1., 0.}}, stationary{};
    require(!ssb_local_coherent_delay(pulse, stationary, stationary, 0., beta),
        "Fast SSB FIR inferred phase from zero modeled energy");

    // A residual below half the smallest bank step can move future center
    // ticks even if successive rate searches continue to choose zero.
    constexpr auto residual_clock = 9.5e-6;
    const SingleCarrierSchedule schedule(profile);
    const auto period = double(schedule.pilot_count + schedule.pilot_stride);
    const auto cycle_symbols = schedule.total_interval_symbols(fast::cycle_intervals(profile));
    const auto slip = residual_clock * cycle_symbols / beta;
    double observation_delay = 0.;
    for (int cycle = 1; cycle <= 2; ++cycle) {
        const auto proposed = ssb_local_coherent_delay(pulse, path, stationary, (cycle - .25) * slip, beta);
        const auto late = ssb_local_coherent_delay(pulse, path, stationary, cycle * slip, beta);
        require(proposed && late && ssb_local_phase_eligible(observation_delay, *proposed, *late, beta, period),
            "Fast SSB FIR rejected independent evidence of sub-grid phase drift");
        require(std::ceil(*proposed / frontend.decimation) != std::ceil(observation_delay / frontend.decimation),
            "Fast SSB FIR phase update did not move an actual frontend center");
        require(!ssb_local_phase_eligible(observation_delay, -*proposed, *late, beta, period),
            "Fast SSB FIR phase accepted movement away from its independent holdout");
        observation_delay = *proposed;
        require(std::abs(observation_delay - *late) < slip / 2.,
            "Fast SSB FIR phase retained accumulated sub-grid drift");
    }
    const auto alias = period / (2. * beta);
    require(!ssb_local_phase_eligible(0., alias, alias, beta, period),
        "Fast SSB FIR phase accepted a nonunique pilot-period neighborhood");

    require(ssb_local_protected_offset(7, 1862, 2, 16200) == 16198 &&
        !ssb_local_protected_offset(7, 1864, 2, 16200) && !ssb_local_protected_offset(8, 0, 2, 16200),
        "Fast SSB FIR used unprotected codeword fill or the next cycle's labels");
    require(!ssb_local_protected_offset(std::numeric_limits<std::size_t>::max(), 0, 2, 16200),
        "Fast SSB FIR accepted a wrapped protected label offset");
}

void check_clean_interval() {
    const auto profile = fast::capacity_profile(fast::Channel::ssb);
    const auto geometry = make_ssb_frontend_geometry(profile.sample_rate, profile.carrier_hz);
    using Bits = std::array<std::uint8_t, fast::physical_interval_bits>;
    std::array<Bits, 2> bits{};
    std::uint64_t state = 0x5a3c2ac45c8e51c9ULL;
    for (auto& interval : bits) for (auto& bit : interval) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        bit = state & 1U;
    }
    std::size_t sent = 0;
    fast::Transmitter transmitter(profile, [&](std::span<std::uint8_t> out) {
        if (sent == bits.size()) return false;
        std::ranges::copy(bits[sent++], out.begin());
        return true;
    });
    std::vector<float> pcm(fast::transmission_samples(profile, bits.size()));
    require(transmitter.read(pcm) == pcm.size(), "Fast SSB local FIR test transmitter stopped early");
    std::vector<C> received;
    SsbFrontend frontend(geometry, [&](const SsbFrontendOutput& output) {
        received.insert(received.end(), output.samples.begin(), output.samples.end());
    });
    require(frontend.push(pcm) == pcm.size(), "Fast SSB local FIR test frontend stopped early");
    const SsbAcquisitionView view{geometry.radius(), geometry.decimation, received};
    // Fix the lock for this conditional equalizer test. The waveform and
    // frontend above are the native transmitter and real-input operator.
    const auto reference = geometry.radius() + 4096;
    SsbAcquisitionLock lock{reference, -double(reference), 0., {{0., 0., 1.}}, {}};
    require(SsbLocalFir::eligible(profile, geometry, lock), "Fast SSB local FIR declined short support");
    auto wide = lock;
    wide.atoms.push_back({profile.sample_rate * .021, 90., {.6, .2}});
    require(!SsbLocalFir::eligible(profile, geometry, wide), "Fast SSB local FIR admitted wide delay support");
    auto long_label = profile;
    long_label.constellation = 1U << 18;
    long_label.pilot_spacing_symbols = 8;
    require(!SsbLocalFir::eligible(long_label, geometry, lock),
        "Fast SSB local FIR admitted a label wider than its demapper storage");
    SsbLocalFir equalizer(profile, geometry, lock);
    const auto holdout_range = equalizer.holdout_centers(0);
    const SsbAcquisitionView holdout_view{holdout_range.first, view.stride, view.samples.subspan(
        (holdout_range.first - view.first_center_input_tick) / view.stride,
        (holdout_range.past_last - holdout_range.first) / view.stride)};
    const auto holdout = equalizer.public_holdout(holdout_view, 0);
    require(holdout && holdout->accepts() && holdout->admission_nmse < .02 && holdout->prospective_nmse < .02,
        "Fast SSB local FIR failed clean held-out public pilots");
    auto mismatched = *holdout;
    mismatched.admission_nmse = .6;
    mismatched.prospective_nmse = .3;
    mismatched.pooled_nmse = .4;
    require(mismatched.accepts(), "Fast SSB local FIR treated residual diagnostics as an admission threshold");
    mismatched.prospective_nmse = std::numeric_limits<double>::quiet_NaN();
    require(!mismatched.accepts(), "Fast SSB local FIR admitted a nonfinite holdout receipt");
    mismatched.prospective_nmse = -.1;
    require(!mismatched.accepts(), "Fast SSB local FIR admitted a negative holdout receipt");
    auto missing_holdout = holdout_view;
    missing_holdout.samples = missing_holdout.samples.first(missing_holdout.samples.size() - 1);
    require(!equalizer.public_holdout(missing_holdout, 0), "Fast SSB local FIR scored incomplete routing context");

    const SingleCarrierSchedule schedule(profile);
    const SsbChannelModel timing(profile, geometry, lock);
    const auto first_pilot = schedule.training_count + schedule.marker_symbols(0) +
        3 * (schedule.pilot_stride + schedule.pilot_count) + schedule.pilot_stride;
    const auto first = timing.center_at_or_after_symbol(first_pilot) - geometry.radius();
    const auto end = timing.center_at_or_after_symbol(first_pilot + schedule.pilot_count) -
        geometry.decimation + geometry.radius() + 1;
    auto interfered_pcm = pcm;
    for (auto tick = first; tick < end; ++tick) {
        const auto time = static_cast<double>(tick) / profile.sample_rate;
        const auto interference = .018 * std::cos(2. * std::numbers::pi * 1810. * time) +
            .012 * std::cos(2. * std::numbers::pi * 2187. * time);
        interfered_pcm[tick] = static_cast<float>(std::clamp(interference, -.016, .016));
    }
    std::vector<C> interfered;
    SsbFrontend interfered_frontend(geometry, [&](const SsbFrontendOutput& output) {
        interfered.insert(interfered.end(), output.samples.begin(), output.samples.end());
    });
    require(interfered_frontend.push(interfered_pcm) == interfered_pcm.size(),
        "Fast SSB local FIR interference control frontend stopped early");
    const auto missing_word = equalizer.public_holdout({view.first_center_input_tick, view.stride, interfered}, 0);
    require(missing_word && !missing_word->admission_present && missing_word->prospective_present &&
        !missing_word->accepts() && missing_word->prospective_nmse == holdout->prospective_nmse,
        "Fast SSB local FIR let a strong public word hide clipped interference in an absent word");
    auto dominated = *missing_word;
    dominated.pooled_nmse = .01;
    require(!dominated.accepts(), "Fast SSB local FIR pooled residual overruled independent word presence");

    auto incomplete = view;
    incomplete.samples = incomplete.samples.first(incomplete.samples.size() / 2);
    require(!equalizer.estimate(incomplete, 0), "Fast SSB local FIR used unobserved lookahead");
    SsbDisplayPoints display(schedule.data_symbols);
    const auto estimate = equalizer.estimate(view, 0, {}, {}, &display);
    require(estimate && estimate->presence.present, "Fast SSB local FIR failed clean predictive presence");
    require(!display.samples().empty() && display.samples().size() <= 512,
        "Fast SSB local FIR did not record bounded actual symbol estimates");
    for (std::size_t i = 0; i < bits.front().size(); ++i)
        require(std::isfinite(estimate->soft_bits[i]) && estimate->soft_bits[i] != 0.F &&
            (estimate->soft_bits[i] > 0.F) == bool(bits.front()[i]), "Fast SSB local FIR changed clean data bits");
    require(equalizer.commit_present(*estimate, view), "Fast SSB local FIR refused a current present proposal");
    require(!equalizer.commit_present(*estimate, view), "Fast SSB local FIR accepted a stale proposal");
    const auto unit = equalizer.quiet_groups(1).front();
    require(equalizer.score_group(view, 1, 0) == SsbPublicPresence::present,
        "Fast SSB local FIR lost a live public quiet-unit control");
    std::vector<C> silence((unit.past_last - unit.first) / geometry.decimation);
    const SsbAcquisitionView quiet{unit.first, geometry.decimation, silence};
    require(equalizer.score_group(quiet, 1, 0) == SsbPublicPresence::absent,
        "Fast SSB local FIR failed complete observed silence");
    auto short_quiet = quiet;
    short_quiet.samples = short_quiet.samples.first(short_quiet.samples.size() - 1);
    require(!equalizer.score_group(short_quiet, 1, 0), "Fast SSB local FIR scored a partial quiet unit");
    std::stop_source stop;
    stop.request_stop();
    require(!equalizer.estimate(view, 1, stop.get_token()), "Fast SSB local FIR ignored cancellation");
}

} // namespace

int main() {
    try {
        check_affine_filter();
        check_routing_aperture();
        check_clock_midpoint();
        check_recurring_observation_phase();
        check_clean_interval();
        std::cout << "Fast SSB local FIR affine operator and clean interval passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
