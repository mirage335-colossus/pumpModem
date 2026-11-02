#!/usr/bin/env python3
"""Actual-table peak and energy envelope bounds for isolated triad v3.

Compile a tiny probe of the existing C++ pulse helper, then evaluate analytical
finite-support synthesis bounds. No transmitter or sampled channel is changed.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import tempfile

import numpy as np

import spread_frontier as frontier


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
HEADER = ROOT/"include/datapump/pattern_pulse.hpp"
SOURCE = ROOT/"src/triad_modem_v3.cpp"
FADING = ROOT/"src/triad_gaussian_fading.hpp"
STREAM = ROOT/"src/triad_simulation_stream.cpp"
PADDING = 1e-9


def actual_table(compiler="c++"):
    header, source = HEADER.read_text(), SOURCE.read_text()
    if "constexpr std::size_t resolution = 256;" not in header or "std::lerp(table[index],table[index+1]" not in header:
        raise ValueError("shared pulse interpolation contract changed; review the proof")
    if "g.carrier_spacing_hz=1.25*g.fine_chip_rate_hz" not in source:
        raise ValueError("carrier spacing changed; review the synthesis Gram")
    gain = float(re.search(r"constexpr double carrier_gain = ([0-9.]+);", source)[1])
    amplitudes = [float(x) for x in re.search(r"array<double,3> amplitude\{([^}]+)\}", source)[1].split(',')]
    with tempfile.TemporaryDirectory(prefix="triad-pulse-bound-") as scratch:
        binary = Path(scratch)/"probe"
        command = [compiler, "-std=c++20", "-O2", "-I", str(ROOT/"include"), "-I", str(ROOT/"src"),
                   str(HERE/"pulse_bound_probe.cpp"), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=30, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output = subprocess.run([str(binary)], check=True, timeout=5,
                                stdout=subprocess.PIPE, text=True).stdout
        table = json.loads(output)
    return table, gain, amplitudes


def exponential_moments(nu, terms=20):
    # Integral_0^1 x^k exp(i*nu*x) dx, k=0,1,2. For |nu|<=.062,
    # the omitted term bound after20 terms is below3e-43 per moment.
    return np.array([sum((1j*nu)**n/(math.factorial(n)*(n+k+1))
                         for n in range(terms)) for k in range(3)])


def gram_entry(knots, resolution, shift, frequency):
    """Integral p(u)*p(u-shift)*exp(2pi*i*frequency*u)du."""
    span = (len(knots)-1)//resolution
    first, last = max(-span, shift-span), min(span, shift+span)
    if first >= last:
        return 0j
    indices = np.arange(first*resolution, last*resolution, dtype=np.int64)
    p0, p1 = knots[np.abs(indices)], knots[np.abs(indices+1)]
    q0, q1 = knots[np.abs(indices-shift*resolution)], knots[np.abs(indices+1-shift*resolution)]
    dp, dq = p1-p0, q1-q0
    moments = exponential_moments(2*math.pi*frequency/resolution)
    values = (p0*q0*moments[0]+(p0*dq+q0*dp)*moments[1]+dp*dq*moments[2])
    values *= np.exp(2j*math.pi*frequency*indices/resolution)/resolution
    return complex(math.fsum(values.real), math.fsum(values.imag))


def synthesis_bounds(table, gain, amplitudes):
    resolution, span = table["resolution"], table["half_span"]
    if resolution != 256 or span != 8 or table["rolloff"] != .25:
        raise ValueError("pulse geometry changed; review the proof")
    knots = np.asarray(table["knots"], dtype=np.float64)
    if len(knots) != span*resolution+1 or not np.isfinite(knots).all():
        raise ValueError("invalid pulse probe")
    sums = []
    for j in range(resolution+1):
        indices = [abs(j-k*resolution) for k in range(-span, span+2)]
        sums.append(math.fsum(abs(knots[index]) for index in indices if index <= span*resolution))
    maximum, phase_index = max((value, j) for j, value in enumerate(sums))
    correlation_rows = []
    for frequency in (0., 1.25, 2.5):
        entries = [gram_entry(knots, resolution, shift, frequency)
                   for shift in range(-2*span, 2*span+1)]
        correlation_rows.append({"carrier_difference_per_chip": frequency,
            "absolute_row_sum": math.fsum(abs(z) for z in entries),
            "entries": [[z.real, z.imag] for z in entries]})
    self_sum, adjacent_sum, outer_sum = [r["absolute_row_sum"] for r in correlation_rows]
    row_bound = self_sum+max(2*adjacent_sum, adjacent_sum+outer_sum)
    amplitude_sum = math.fsum(amplitudes)
    coefficient_norm_squared = gain*gain*math.fsum(a*a for a in amplitudes)
    peak_amplitude = gain*amplitude_sum*(maximum+PADDING)+PADDING
    return {"sum_absolute_pulse_max_unpadded": maximum, "maximizing_phase_grid_index": phase_index,
        "phase_grid_resolution": resolution, "pulse_endpoint_value": float(knots[-1]),
        "lookup_piecewise_linear_energy": gram_entry(knots, resolution, 0, 0).real,
        "carrier_gain": gain, "amplitudes": amplitudes, "chip_triplet_coefficient_norm_squared": coefficient_norm_squared,
        "absolute_peak_amplitude_bound": peak_amplitude,
        "absolute_peak_power_bound": peak_amplitude*peak_amplitude,
        "synthesis_Gram_row_bound_unpadded": row_bound,
        "synthesis_energy_operator_bound": row_bound+PADDING,
        "numerical_absolute_padding": PADDING,
        "gram_rows": correlation_rows}


def envelope_information_bound(duration, fine_rate, peak_power, energy_operator, coefficient_norm,
                               cn0, width, spectrum, excess_delay_seconds=.000005):
    # ceil(T*R) safely includes every nonzero fine-cell center. This permits one
    # extra coefficient beyond active elapsed time instead of assuming E=T.
    count_upper = math.ceil(duration*fine_rate)
    source_energy_upper = energy_operator*coefficient_norm*count_upper/fine_rate
    observation = duration+excess_delay_seconds
    J, I, _ = frontier.correlation_integrals(observation, width, spectrum)
    kl = min(cn0*source_energy_upper, cn0*cn0*peak_power*source_energy_upper*J,
             cn0*cn0*peak_power*peak_power*I)
    return {"active_elapsed_seconds": duration, "fine_rate_hz_per_carrier": fine_rate,
        "nonzero_coefficient_count_upper": count_upper,
        "source_energy_seconds_upper": source_energy_upper,
        "source_average_power_upper": source_energy_upper/duration,
        "full_delayed_observation_seconds": observation,
        "Gaussian_channel_KL_upper_nats": kl,
        "absolute_peak_power_bound": peak_power}


def interpolation_absolute_gain(phase):
    """L1 gain of the inspected stream's normalized16-tap Blackman/sinc row."""
    phase = phase % 1.
    if phase == 0:
        return 1.
    coefficients = []
    for offset in range(-7, 9):
        d = phase-offset
        if abs(d) >= 8:
            continue
        z = (d+8)/16
        window = .42-.5*math.cos(2*math.pi*z)+.08*math.cos(4*math.pi*z)
        coefficients.append((1. if abs(d)<1e-12 else math.sin(math.pi*d)/(math.pi*d))*window)
    return math.fsum(abs(c) for c in coefficients)/abs(math.fsum(coefficients))


def discrete_fir_peak_bound(duration, cn0, peak_power, kernel):
    """Peak-only bound for the actual finite Gaussian FIR's discrete covariance.

    No continuous Gram-energy estimate is inserted into this sample law.
    r[l]=sum h[k]h[k+l]. The covariance triangle bound gives
    tr(K²)<=(C*dt*P)²*(N*r[0]²+2*sum_(l=1..N-1)(N-l)*r[l]²).
    """
    dt = kernel["sample_interval_seconds"]
    coefficients = np.asarray(kernel["coefficients"], dtype=np.float64)
    covariance = np.array([math.fsum(coefficients[:len(coefficients)-lag]*coefficients[lag:])
                           for lag in range(len(coefficients))])
    # Explicit aligned direct lattice; the delayed ray is fractional. Include
    # its interpolation L1 gain and both8-sample source reconstruction tails.
    interpolation_gain = max(interpolation_absolute_gain(0.),
                             interpolation_absolute_gain(-.000005/dt))+PADDING
    effective_peak = peak_power*interpolation_gain**2
    count = math.ceil((duration+.000005)/dt)+16
    total = count*covariance[0]**2+2*math.fsum((count-lag)*covariance[lag]**2
        for lag in range(1, min(count, len(covariance))))
    # Pad the covariance reduction; the source peak was padded separately.
    trace_square_upper = (cn0*dt*effective_peak)**2*(total*(1+PADDING)+PADDING)
    energy_upper = effective_peak*count*dt
    kl = min(cn0*energy_upper*(covariance[0]+PADDING), .5*trace_square_upper)
    return {"sample_interval_seconds": dt, "sample_count_upper": count,
        "width_2sigma_hz": kernel["width_2sigma_hz"], "FIR_tap_count": len(coefficients),
        "FIR_coefficient_sha256": hashlib.sha256(coefficients.astype("<f8").tobytes()).hexdigest(),
        "FIR_covariance_zero": float(covariance[0]),
        "source_interpolation_absolute_gain_bound": interpolation_gain,
        "interpolated_source_absolute_peak_power_bound": effective_peak,
        "source_discrete_energy_seconds_upper": energy_upper,
        "Gaussian_discrete_KL_upper_nats": kl,
        "one_completion_admission_probability_upper": {
            p["id"]: frontier.upper_power_given_kl(kl,p["false_probability_used_in_necessity"])
            for p in frontier.profiles() if "global" in p["id"]},
        "scope": "Actual finite-FIR covariance, zero sample-clock error and an integer-aligned direct arrival at the stated physicaldt,5us delayed ray, unit sourcegain, extra gain/perturbation/frontend stages off; nominalC=1e-6,T=tenyears. Includes fixed fractional delayed-ray interpolation L1 gain and both8-sample tails. Uses peak-only discreteenergy, not the continuous pulse-Gram estimate; does not cover arbitrary fractional arrival or clock drift."}


def report(compiler="c++"):
    table, gain, amplitudes = actual_table(compiler)
    bounds = synthesis_bounds(table, gain, amplitudes)
    examples = []
    for inner_rate in (.036, .0036, .00036, .000036):
        for spectrum in ("gaussian", "ou"):
            example = envelope_information_bound(frontier.MAX_SECONDS, inner_rate/6,
                bounds["absolute_peak_power_bound"], bounds["synthesis_energy_operator_bound"],
                bounds["chip_triplet_coefficient_norm_squared"], 1e-6, .001, spectrum)
            examples.append({"GUI_inner_rate_hz": inner_rate, "spectrum": spectrum,
                "cn0_db_hz_nominal_power_one": -60., "Gaussian_width_2sigma_hz_or_matched_HWHM": .001,
                **example, "one_completion_admission_probability_upper": {
                    p["id"]: frontier.upper_power_given_kl(example["Gaussian_channel_KL_upper_nats"],
                        p["false_probability_used_in_necessity"])
                    for p in frontier.profiles() if "global" in p["id"]}})
    paths = (Path(__file__), HERE/"pulse_bound_probe.cpp", HEADER, SOURCE, FADING, STREAM, HERE/"spread_frontier.py")
    return {"schema": "triad-v3-actual-table-envelope-bound-v1",
        "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
        "pulse_table_sha256": hashlib.sha256(np.asarray(table["knots"], dtype="<f8").tobytes()).hexdigest(),
        "bounds": bounds, "ten_year_minus60_examples": examples,
        "discrete_FIR_peak_only_examples": [discrete_fir_peak_bound(frontier.MAX_SECONDS, 1e-6,
            bounds["absolute_peak_power_bound"], kernel) for kernel in table["gaussian_fir"]],
        "derivation": [
            "Each fine chip permutes the same three amplitudes; their absolute sum is A and their squared sum is3 up to the actual FP64 constants. Data encryption and phase rotations do not increase these bounds.",
            "The three modulation phasors have unit magnitude. With gain g and common envelope e(t) in[0,1], |x(t)|<=g*A*sum_k|p(u-k)|, u=t*fine_rate-.5.",
            "Within every interval between phase knots j/256, each shifted table pulse is affine, hence its absolute value and their sum are convex. Maxima lie at knot endpoints. Both support endpoints are explicitly included; their discontinuous truncation cannot increase an interior supremum.",
            "For energy, consider the finite synthesis family p(u-k)*exp(2pi*i*f_c*u), f_c in{-1.25,0,1.25}. Its Gram entries have magnitudes depending only on integer lag and carrier difference0,1.25,2.5.",
            "The Gram spectral norm is at most its maximum absolute row sum B. Finite chip sequences are principal submatrices of this family. Thus integral|x|²dt<=B*g²*sum_chip,sum_carrier|a|²/fine_rate<=B*(3g²)*ceil(T*fine_rate)/fine_rate. Multiplication by an envelope bounded by one cannot increase energy.",
            "Each Gram integrand is a quadratic polynomial times an exponential on1/256-chip cells. The exponential moments use20 power-series terms; |nu|<=2pi*2.5/256. FP64 summations use fsum, with an extra1e-9 engineering padding on pulse and Gram bounds.",
            "For independent Gaussian ray gains with equal nominal powers, source energy<=E and absolute squared envelope<=P give D<=min(C*E,C²*P*E*J,C²*P²*I), where J and I use the complete delayed observation support. This uses absolute nominal-power bounds, not an unproved PAPR or E<=T claim."],
        "scope_and_limits": [
            "Principal use: isolatedv3 Added1,FHSSoff,burst off; the inspected source envelopes only reduce amplitude. No source normalization to exact mean power1 is assumed.",
            "The energy bound applies to actual active elapsed durationT, after private-tile rounding, not a shorter requested duration. A ten-year requested duration may round above ten years and needs its actualT substituted.",
            "The actual shared pulse table is probed from the checked source revision. The bound concerns the continuous piecewise-linear source model; it does not certify physical DAC reconstruction or additional analog filters.",
            "The continuous Gram-energy bound is not automatically a bound on point-sampled energy. Discrete examples instead use peak times samplecount and the actual probed Gaussian FIR covariance, at the explicitly stated physical sample interval.",
            "Table/Gram calculations and padding are numerical analytical evidence, not a formal interval-arithmetic proof covering every compiler/libm or extreme coordinate roundoff behavior.",
            "The Gaussian-channel information connection additionally assumes the continuous normalized scatter correlation, known white noise, fixedsource waveform, and no unaccounted modulation/gain stage. FIR spectral approximation, oscillator steering, frequency-selective perturbation and frontend processing need their own model relation.",
            "Stochastic channel gain is unbounded; P andE bound the transmitted deterministic envelope, not the faded received realization. Independent zero-mean ray gains are marginalized in the Gaussian covariance.",
            "No receiver success rate is inferred. No ordinary Robust implementation is changed."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    args.output.write_text(json.dumps(report(args.compiler), indent=2, sort_keys=True, allow_nan=False)+"\n")


if __name__ == "__main__":
    main()
