#!/usr/bin/env python3
"""Conservative generated-PCM peak bound for current 256-APSK / span-16 RRC.

No audio device is opened. This reproduces the production constellation radii
and 2048-knot-per-symbol linearly interpolated pulse from src/fast/modem.cpp.
The bound is upstream of optional resampling and downstream mixer/software gain.
"""
import json
import math


def rrc(t, alpha):
    if abs(t) < 1e-10:
        return 1 + alpha * (4 / math.pi - 1)
    if abs(abs(4 * alpha * t) - 1) < 1e-8:
        return alpha / math.sqrt(2) * (
            (1 + 2 / math.pi) * math.sin(math.pi / (4 * alpha))
            + (1 - 2 / math.pi) * math.cos(math.pi / (4 * alpha)))
    return (math.sin(math.pi * t * (1 - alpha))
            + 4 * alpha * t * math.cos(math.pi * t * (1 + alpha))) / (
                math.pi * t * (1 - 16 * alpha * alpha * t * t))


def bound(rolloff):
    populations = (4, 12, 20, 28, 36, 44, 52, 60)
    radii = (1, 2.8, 4.6, 6.4, 8.2, 10, 11.8, 13.6)
    mean_energy = sum(n * r * r for n, r in zip(populations, radii)) / 256
    maximum_radius = max(radii) / math.sqrt(mean_energy)
    resolution, radius = 2048, 8
    table = [rrc(i / resolution, rolloff)
             for i in range(radius * resolution + 2)]
    # Every shifted pulse has its interpolation knots at the same fractional
    # symbol coordinates. Within each common segment, sum(abs(linear)) is
    # convex, so its maximum is at a knot. Support-end discontinuities only
    # remove nonnegative terms away from the exactly sampled endpoint.
    pulse_sums = [
        sum(abs(table[abs(q - resolution * n)]) for n in range(-radius, radius + 2)
            if abs(q - resolution * n) <= radius * resolution)
        for q in range(resolution)
    ]
    largest = max(pulse_sums)
    multiplier = maximum_radius * largest
    return {
        "rolloff": rolloff,
        "maximum_constellation_radius": maximum_radius,
        "maximum_absolute_pulse_sum": largest,
        "maximizing_fractional_symbol_phase": pulse_sums.index(largest) / resolution,
        "generated_pcm_peak_bound_per_amplitude": multiplier,
        "unity_peak_amplitude_ceiling": 1 / multiplier,
        "amplitude_ceiling_for_peak_0_95": 0.95 / multiplier,
        "peak_bound_at_amplitude": {str(a): a * multiplier for a in (0.5, 0.35, 0.3)},
    }


if __name__ == "__main__":
    print(json.dumps({
        "scope": "Bound for generated PCM, before optional resampling or external gain",
        "method": "abs(real(exp(i*phase)*sum(symbol*pulse))) <= max_symbol_radius*sum(abs(pulse))",
        "continuous_phase_coverage": "Maximum of convex piecewise-linear absolute pulse sum occurs at a table knot",
        "numerical_scope": "Double-precision evaluation; headroom recommendations leave substantial rounding margin",
        "constellation": 256,
        "pulse_span_symbols": 16,
        "table_knots_per_symbol": 2048,
        "results": [bound(0.2), bound(0.1)],
    }, indent=2) + "")
