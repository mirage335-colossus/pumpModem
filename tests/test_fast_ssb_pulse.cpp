#include "datapump/fast/modem.hpp"
#include "fast/ssb_pulse.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
namespace fast = datapump::fast;
using namespace datapump::fast::detail;
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void close(Complex actual, Complex expected, double tolerance, const char* message) {
    require(std::abs(actual - expected) <= tolerance * std::max(1., std::abs(expected)), message);
}

template<class Action>
void invalid(Action action) {
    try { action(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Fast SSB pulse accepted invalid geometry or coordinates");
}

Complex phase(double hz, double ticks, double input_rate) {
    return std::polar(1., tau * hz * ticks / input_rate);
}

double direct_tx_pulse(const fast::Profile& p, double time) {
    time = std::abs(time);
    if (time > single_carrier_pulse_radius(p)) return 0.;
    // Independent evaluation of the native TX interpolation from its
    // documented grid, without calling the pulse operator or its table.
    constexpr double resolution = 2048.;
    const auto bin = std::floor(time * resolution);
    const auto fraction = time * resolution - bin;
    const auto left = fast::root_raised_cosine(bin / resolution, p.rolloff);
    const auto right = fast::root_raised_cosine((bin + 1.) / resolution, p.rolloff);
    return left * (1. - fraction) + right * fraction;
}

Complex direct_atom(const fast::Profile& p, const SsbFrontendGeometry& g, double u0,
                    double t_minus_ref, double clock, double hz) {
    const auto beta = p.symbol_rate / p.sample_rate;
    const auto radius = static_cast<int>(g.radius());
    Complex result{};
    // Evaluate the clocked native transmit pulse and Doppler at the actual
    // input tick of each tap, then perform the convolution.
    for (int j = -radius; j <= radius; ++j) {
        const auto t = t_minus_ref + j;
        const auto pulse = direct_tx_pulse(p, u0 + beta * (j + clock * t));
        result += g.taps[j + radius] * pulse * phase(hz, t, g.input_rate);
    }
    return p.amplitude * result;
}

void check_native_analytic_waveform() {
    const auto p = fast::capacity_profile(fast::Channel::ssb);
    const auto g = make_ssb_frontend_geometry(p.sample_rate, p.carrier_hz);
    const SsbPulse pulse(p, g);
    const SingleCarrierSchedule schedule(p);
    const auto beta = p.symbol_rate / p.sample_rate;
    const auto radius = single_carrier_pulse_radius(p);
    fast::Transmitter tx(p, [](std::span<std::uint8_t>) { return false; });
    std::vector<std::complex<float>> analytic(fast::transmission_samples(p, 0));
    require(tx.read_analytic(analytic) == analytic.size(), "Native SSB preamble length changed");
    constexpr int delay_ticks = 13;
    constexpr double doppler_hz = 117.125;
    constexpr int reference_tick = 991;
    const auto cache = pulse.make_cache(0., doppler_hz);
    const auto center_at = [&](std::size_t i) {
        return static_cast<int>(g.radius() + i * g.decimation);
    };
    const auto last = (analytic.size() - g.radius()) / g.decimation;
    const auto filter_radius = static_cast<int>(g.radius());
    for (const auto output_index : {std::size_t{0}, std::size_t{137}, std::size_t{2581},
                                    last, last + 25, last + g.radius() + 1}) {
        const auto center = center_at(output_index);
        Complex reference{};
        for (int j = -filter_radius; j <= filter_radius; ++j) {
            const auto input_tick = center + j;
            const auto tx_tick = input_tick - delay_ticks;
            if (tx_tick < 0 || static_cast<std::size_t>(tx_tick) >= analytic.size()) continue;
            const auto baseband = static_cast<Complex>(analytic[tx_tick]) *
                phase(-p.carrier_hz, tx_tick, p.sample_rate);
            reference += g.taps[j + filter_radius] * baseband *
                phase(doppler_hz, input_tick - reference_tick, p.sample_rate);
        }
        Complex modeled{}, cached{};
        for (std::size_t symbol = 0; symbol < schedule.training_count; ++symbol) {
            const auto u0 = (center - delay_ticks) * beta - radius - symbol;
            const auto known = schedule.training_symbol(symbol);
            modeled += known * pulse.evaluate(u0, center - reference_tick, 0., doppler_hz).value;
            cached += known * cache.evaluate(u0, center - reference_tick).value;
        }
        close(modeled, reference, 2e-7, "Filtered native analytic SSB preamble differs from its pulse atoms");
        close(cached, reference, 4e-6, "Cached pulse changed the filtered native SSB preamble");
    }
}

void check_narrow_acoustic_pulse() {
    auto p = fast::capacity_profile(fast::Channel::acoustic_short);
    p.acoustic_ofdm = false;
    p.ofdm_training_blocks = 16;
    p.ldpc_frame_bits = 1944;
    p.constellation = 4;
    p.carrier_hz = 1800.;
    p.symbol_rate = 183.;
    const auto g = make_ssb_frontend_geometry(p.sample_rate,p.carrier_hz,210.,47);
    require(single_carrier_low_rate_decimation(p) == 8, "Narrow pulse test missed the interpolated transmitter");
    const SsbPulse pulse(p,g);
    const SingleCarrierSchedule schedule(p);
    constexpr double frequency = 69.;
    const auto cache = pulse.make_cache(0.,frequency);
    const auto beta = p.symbol_rate/p.sample_rate;
    const auto radius = single_carrier_pulse_radius(p);
    fast::Transmitter tx(p,[](std::span<std::uint8_t>){return false;});
    std::array<std::complex<float>,16384> analytic{};
    require(tx.read_analytic(analytic) == analytic.size(), "Narrow pulse test lost its native preamble");

    double reference_power=0.,error_power=0.;
    for (int center=static_cast<int>(g.radius());center+g.radius()<analytic.size();center+=g.decimation) {
        Complex reference{};
        for (int k=-static_cast<int>(g.radius());k<=static_cast<int>(g.radius());++k) {
            const auto tick=center+k;
            reference+=g.taps[k+g.radius()]*static_cast<Complex>(analytic[tick])*
                phase(frequency-p.carrier_hz,tick,p.sample_rate);
        }
        const auto symbol_time=center*beta-radius;
        const auto first=std::max(0,int(std::ceil(symbol_time-cache.support_symbols())));
        const auto last=int(std::floor(symbol_time+cache.support_symbols()));
        Complex predicted{};
        for (int symbol=first;symbol<=last;++symbol)
            predicted+=schedule.training_symbol(symbol)*cache.evaluate(symbol_time-symbol,center).value;
        reference_power+=std::norm(reference);
        error_power+=std::norm(reference-predicted);
    }
    require(reference_power > 0. && error_power/reference_power < 1e-6,
        "Continuous RRC exceeded the measured error allowance for the interpolated acoustic preamble");
    std::cout << "Narrow acoustic filtered pulse NMSE " << error_power/reference_power << '\n';
}

void check_derivatives() {
    auto p = fast::capacity_profile(fast::Channel::ssb);
    p.sample_rate = 44100;
    p.amplitude = .27;
    const auto g = make_ssb_frontend_geometry(p.sample_rate, p.carrier_hz, 1520.);
    const SsbPulse pulse(p, g);
    const auto beta = p.symbol_rate / p.sample_rate;
    constexpr double delay_ticks = 13.75;
    constexpr double clock = 125e-6;
    constexpr double hz = -134.75;
    const auto center = double(g.radius() + 1000 * g.decimation);
    const auto unwarped = (center - delay_ticks) * beta - single_carrier_pulse_radius(p);
    const auto u0 = unwarped - std::floor(unwarped);
    for (const auto t_minus_ref : {center + 63., 0.}) {
        const auto actual = pulse.evaluate(u0, t_minus_ref, clock, hz);
        close(actual.value, direct_atom(p, g, u0, t_minus_ref, clock, hz), 2e-13,
            "Fractionally delayed, clocked, Doppler-shifted pulse differs from direct FIR");

        // Keep central-difference phase curvature below the derivative
        // tolerance at the longer input-tick distance.
        constexpr double offset_step = 1e-5, clock_step = 2e-9, frequency_step = 1e-4;
        const auto offset = (direct_atom(p, g, u0 + offset_step * beta, t_minus_ref, clock, hz) -
                             direct_atom(p, g, u0 - offset_step * beta, t_minus_ref, clock, hz)) /
                            (2 * offset_step);
        const auto clock_fd = (direct_atom(p, g, u0, t_minus_ref, clock + clock_step, hz) -
                               direct_atom(p, g, u0, t_minus_ref, clock - clock_step, hz)) /
                              (2 * clock_step);
        const auto frequency_fd = (direct_atom(p, g, u0, t_minus_ref, clock, hz + frequency_step) -
                                   direct_atom(p, g, u0, t_minus_ref, clock, hz - frequency_step)) /
                                  (2 * frequency_step);
        close(actual.offset_ticks_derivative, offset, 2e-6, "Filtered pulse offset derivative changed");
        close(actual.clock_ratio_derivative, clock_fd, 2e-5, "Filtered pulse clock derivative lost tap time");
        close(actual.frequency_hz_derivative, frequency_fd, 2e-9, "Filtered pulse frequency derivative lost tap time");
        if (t_minus_ref == 0.) {
            require(std::abs(clock_fd) > 1e-5 && std::abs(frequency_fd) > 1e-7,
                "Zero center time must still exercise nonzero FIR tap derivatives");
        }
    }
}

void check_support_and_geometry() {
    const auto p = fast::capacity_profile(fast::Channel::ssb);
    const auto g = make_ssb_frontend_geometry(p.sample_rate, p.carrier_hz);
    const SsbPulse pulse(p, g);
    const auto bytes = pulse.workspace_bytes();
    const auto radius = single_carrier_pulse_radius(p);
    constexpr double clock = -310e-6;
    const auto support = pulse.support_symbols(clock);
    require(support == radius + g.radius() * ((1. + clock) * (p.symbol_rate / p.sample_rate)),
        "Fast SSB forward support omitted frontend taps or clock");
    bool nonzero_tail = false;
    for (const auto displacement : {radius + .33, radius + 1.41, -radius - 2.35}) {
        const auto actual = pulse.evaluate(displacement, 0., clock, 82.);
        const auto expected = direct_atom(p, g, displacement, 0., clock, 82.);
        close(actual.value, expected, 2e-13, "Fast SSB FIR tail differs from direct filtering");
        nonzero_tail |= std::abs(expected) > 1e-9;
    }
    require(nonzero_tail, "Filtered support test did not reach the FIR extension");
    for (const auto displacement : {-support - .001, support + .001}) {
        const auto x = pulse.evaluate(displacement, 0., clock, 82.);
        require(x.value == Complex{} && x.offset_ticks_derivative == Complex{} &&
                x.clock_ratio_derivative == Complex{} && x.frequency_hz_derivative == Complex{},
            "Fast SSB forward pulse or derivative leaked beyond its finite support");
    }
    require(pulse.workspace_bytes() == bytes, "Fast SSB pulse workspace grew with queries");

    auto low_rate = p;
    low_rate.ldpc_frame_bits = 64800;
    low_rate.symbol_rate = 2.;
    fast::validate(low_rate);
    invalid([&] { (void)SsbPulse(low_rate, g); });
    auto wrong_rate = g;
    wrong_rate.input_rate = 44100;
    invalid([&] { (void)SsbPulse(p, wrong_rate); });
    auto bad_taps = g;
    bad_taps.taps.front() = .125;
    invalid([&] { (void)SsbPulse(p, bad_taps); });
    invalid([&] { (void)pulse.evaluate(0., 0., -1., 0.); });
    invalid([&] { (void)pulse.evaluate(0., 0., 0., std::numeric_limits<double>::infinity()); });
}

void check_cache_against_direct() {
    auto p = fast::capacity_profile(fast::Channel::ssb);
    p.sample_rate = 44100;
    const auto g = make_ssb_frontend_geometry(p.sample_rate, p.carrier_hz, 1520.);
    const SsbPulse pulse(p, g);
    for (const auto& [clock, frequency] : {std::pair{125e-6, -134.75}, std::pair{-305e-6, 92.37}}) {
        const auto cache = pulse.make_cache(clock, frequency);
        require(cache.support_symbols() == pulse.support_symbols(clock) && cache.workspace_bytes() < 1024 * 1024,
            "Fast SSB cache lost its public support or workspace bound");
        std::array<double, 4> error{}, scale{};
        for (int k = -76; k <= 76; ++k) {
            const auto t = 19000. + k * 133.17;
            const auto u = .0784 * k + .131;
            const auto exact = pulse.evaluate(u, t, clock, frequency);
            const auto cached = cache.evaluate(u, t);
            const std::array<Complex, 4> reference{exact.value, exact.offset_ticks_derivative,
                exact.clock_ratio_derivative, exact.frequency_hz_derivative};
            const std::array<Complex, 4> observed{cached.value, cached.offset_ticks_derivative,
                cached.clock_ratio_derivative, cached.frequency_hz_derivative};
            for (std::size_t i = 0; i < reference.size(); ++i) {
                error[i] += std::norm(observed[i] - reference[i]);
                scale[i] += std::norm(reference[i]);
            }
        }
        require(std::sqrt(error[0] / scale[0]) < 1e-4, "Cached Fast SSB pulse missed direct values");
        for (std::size_t i = 1; i < error.size(); ++i)
            require(std::sqrt(error[i] / scale[i]) < .005, "Cached Fast SSB pulse lost a tap-time derivative");
        for (const auto sign : {-1., 1.}) {
            const auto outside = cache.evaluate(sign * (cache.support_symbols() + .001), 0.);
            require(outside.value == Complex{} && outside.offset_ticks_derivative == Complex{} &&
                    outside.clock_ratio_derivative == Complex{} && outside.frequency_hz_derivative == Complex{},
                "Cached Fast SSB pulse leaked beyond the exact support");
        }
        const auto support = cache.support_symbols();
        const auto lattice = 17. + 33. / SsbPulseCache::samples_per_symbol;
        const auto infinity = std::numeric_limits<double>::infinity();
        for (const auto first : {support, support - .137, lattice, std::nextafter(lattice, infinity),
                std::nextafter(lattice, -infinity), .145, 0., -.123, -lattice, -support}) {
            auto count = static_cast<std::size_t>(std::floor(first + support)) + 1;
            if (first - double(count - 1) < -support) --count;
            const auto run = cache.symbol_run(first, count);
            require(bool(run), "Fast SSB integer-symbol run rejected its supported geometry");
            for (std::size_t k = 0; k < count; ++k) {
                const auto exact = cache.kernel(first - double(k));
                const auto derivative = run->derivatives(k);
                close(run->value(k), exact.value, 2e-12, "Fast SSB integer run changed cubic interpolation");
                close(run->tap_moment(k), exact.tap_moment, 2e-10, "Fast SSB integer run changed the FIR tap moment");
                close(derivative.symbol, exact.symbol_derivative, 2e-10, "Fast SSB integer run changed the pulse derivative");
                close(derivative.tap_moment, exact.tap_moment_derivative, 2e-10,
                    "Fast SSB integer run changed the FIR tap-time derivative");
            }
        }
        require(!cache.symbol_run(support + .001, 1) && !cache.symbol_run(-support, 2) &&
                !cache.symbol_run(0., 0) && !cache.symbol_run(infinity, 1),
            "Fast SSB integer run admitted a clipped or invalid coordinate");

        std::array<SsbPulseKernelValue, 400> row;
        const auto same_bits = [](Complex a, Complex b) {
            return std::bit_cast<std::uint64_t>(a.real()) == std::bit_cast<std::uint64_t>(b.real()) &&
                std::bit_cast<std::uint64_t>(a.imag()) == std::bit_cast<std::uint64_t>(b.imag());
        };
        for (const auto first : {support + 1.1, std::nextafter(support, infinity), support,
                std::nextafter(support, -infinity), lattice, std::nextafter(lattice, infinity),
                std::nextafter(lattice, -infinity), .145, 0., -.123, -lattice,
                std::nextafter(-support, infinity), -support, std::nextafter(-support, -infinity)}) {
            cache.half_lattice_values(first, row);
            for (std::size_t k = 0; k < row.size(); ++k) {
                const auto reference = cache.kernel(first - .5 * k);
                require(same_bits(row[k].value, reference.value) && same_bits(row[k].moment, reference.tap_moment),
                    "Fast SSB half-lattice changed a scalar pulse value or tap moment");
            }
        }
    }
}

} // namespace

int main() {
    check_native_analytic_waveform();
    check_narrow_acoustic_pulse();
    check_derivatives();
    check_support_and_geometry();
    check_cache_against_direct();
    std::cout << "Fast SSB filtered pulse tests passed\n";
}
