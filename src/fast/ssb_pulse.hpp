#pragma once

#include "search_fft.hpp"
#include "single_carrier_waveform.hpp"
#include "ssb_frontend.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace datapump::fast::detail {

// The bounded narrow acoustic fit uses the continuous RRC approximation for
// the existing oversampled, cubic-interpolated transmit pulse.
inline bool supports_narrow_acoustic_fit(const Profile& p) noexcept {
    return p.channel == Channel::acoustic_short && p.sample_rate == 48000 &&
        p.capacity_mode && !p.acoustic_ofdm && compact_acoustic_framing(p) &&
        p.constellation == 4 && p.symbol_rate >= 180. && p.symbol_rate < 800. &&
        p.rolloff >= .10 && p.pilot_spacing_symbols <= 64;
}

struct SsbPulseEvaluation {
    std::complex<double> value{};
    // The offset advances the unwarped source position by one input tick.
    // An excess delay of one input tick has the opposite derivative.
    std::complex<double> offset_ticks_derivative{};
    std::complex<double> clock_ratio_derivative{};
    std::complex<double> frequency_hz_derivative{};
};

struct SsbPulseKernelSample {
    std::complex<double> value{};
    std::complex<double> symbol_derivative{};
    std::complex<double> tap_moment{};
    std::complex<double> tap_moment_derivative{};
};

struct SsbPulseKernelValue {
    std::complex<double> value{};
    std::complex<double> moment{};
};

// One explicitly owned clock/frequency state. The unrotated kernel and
// its first FIR tap moment share a finite uniform symbol-time grid.
class SsbPulseCache {
public:
    static constexpr std::size_t samples_per_symbol = 128;
    static constexpr std::size_t maximum_fft_size = 1U << 18;

    class SymbolRun {
    public:
        using Complex = std::complex<double>;
        struct Derivatives { Complex symbol, tap_moment; };

        // k is the zero-based position in the admitted run. The parent
        // cache must remain alive and unchanged while the run is used.
        Complex value(std::size_t k) const noexcept { return interpolate(values_, weights_, k); }
        Complex tap_moment(std::size_t k) const noexcept { return interpolate(moments_, weights_, k); }
        Derivatives derivatives(std::size_t k) const noexcept {
            return {interpolate(values_, slopes_, k), interpolate(moments_, slopes_, k)};
        }

    private:
        friend class SsbPulseCache;
        SymbolRun(const Complex* values, const Complex* moments, std::array<double, 4> weights,
                  std::array<double, 4> slopes)
            : values_(values), moments_(moments), weights_(weights), slopes_(slopes) {}
        static Complex interpolate(const Complex* grid, const std::array<double, 4>& weights,
                                   std::size_t k) noexcept {
            const auto* tap = grid - k * samples_per_symbol;
            return tap[0] * weights[0] + tap[1] * weights[1] + tap[2] * weights[2] + tap[3] * weights[3];
        }
        const Complex* values_;
        const Complex* moments_;
        std::array<double, 4> weights_, slopes_;
    };

    SsbPulseEvaluation evaluate(double displacement_symbols, double ticks_from_reference) const;
    SsbPulseKernelSample kernel(double clocked_displacement_symbols) const;
    // Evaluate the value and tap moment at first - 0.5*k for each output k.
    // Each coordinate keeps its own cubic cell and finite support check.
    void half_lattice_values(double first, std::span<SsbPulseKernelValue> output) const;
    // Admits kernel(first-k) for k=0..count-1. All displacements must be
    // within the finite support of this cache.
    std::optional<SymbolRun> symbol_run(double first, std::size_t count) const & noexcept;
    std::optional<SymbolRun> symbol_run(double, std::size_t) const && = delete;
    double support_symbols() const noexcept { return support_; }
    double clock_ratio_error() const noexcept { return clock_; }
    double frequency_hz() const noexcept { return frequency_; }
    std::size_t workspace_bytes() const noexcept;

private:
    friend class SsbPulse;
    SsbPulseCache(double input_rate, double symbols_per_tick, double clock, double frequency,
                  double support, std::size_t half_samples,
                  std::vector<std::complex<double>> values, std::vector<std::complex<double>> moments);

    double input_rate_, symbols_per_tick_, clock_, frequency_, support_;
    std::size_t half_samples_;
    std::vector<std::complex<double>> values_, moments_;
};

// Immutable preparation for applying the finite pulse to a repeating symbol word.
class SsbPeriodicPulse {
public:
    std::size_t workspace_bytes() const noexcept;

private:
    friend class SsbPulse;
    explicit SsbPeriodicPulse(std::span<const std::complex<double>> word);

    modem::detail::search_fft::DoublePlan plan_;
    std::vector<std::complex<double>> word_;
    std::vector<std::complex<double>> spectrum_;
};

struct SsbPeriodicWave {
    std::vector<std::complex<double>> values;
    std::vector<std::complex<double>> moments;
};

// Direct reference for a single analytic transmit symbol after residual
// Doppler and the receive frontend's centered full-input-rate FIR. The
// returned atom includes transmit amplitude. Its channel gain is applied by
// the caller. Owns its finite TX table and a copy of the frontend coefficients.
class SsbPulse {
public:
    SsbPulse(const Profile&, const SsbFrontendGeometry&);

    // displacement_symbols is the unwarped symbol displacement at the
    // centered input tick t, with delay in nominal source ticks and the TX
    // pulse radius already subtracted. The displacement is held fixed when
    // differentiating the clock. ticks_from_reference is t-tc on the full input grid,
    // including when t is one of the frontend's radius+k*decimation centers.
    // A positive clock ratio error advances the source by error*(t-tc) ticks.
    // frequency_hz is the residual rotation after mixing.
    //
    // Timing derivatives describe the shared piecewise-linear TX table
    // away from its knots and finite support cutoff.
    SsbPulseEvaluation evaluate(double displacement_symbols, double ticks_from_reference,
                                double clock_ratio_error, double frequency_hz) const;

    // Support is measured in clocked symbol displacement from the pulse
    // center. The FIR can extend it beyond the finite transmit pulse radius.
    double support_symbols(double clock_ratio_error) const;
    // Interpolated approximation of the finite FIR at this clock/frequency.
    SsbPulseCache make_cache(double clock_ratio_error, double frequency_hz,
                             std::stop_token stop = {}) const;
    SsbPeriodicPulse prepare_periodic(std::span<const std::complex<double>> word) const;
    void make_periodic(const SsbPeriodicPulse&, double clock_ratio_error, double frequency_hz,
                       SsbPeriodicWave&, std::stop_token stop = {}) const;
    struct CacheStorage { std::size_t retained, temporary; };
    CacheStorage cache_storage_bound(double max_abs_clock_ratio) const;
    std::size_t workspace_bytes() const noexcept;

private:
    double step(double clock_ratio_error) const;

    RrcPulseTable transmit_;
    std::vector<double> taps_;
    double input_rate_;
    double symbols_per_tick_;
    double amplitude_;
};

} // namespace datapump::fast::detail
