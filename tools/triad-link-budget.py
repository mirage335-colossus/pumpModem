#!/usr/bin/env python3
"""Parameterized radiated-power link budget and optimistic detection reference.

This computes received C/N0 and nominal E/N0 from radiated power, path loss,
noise density, and useful waveform time. Its detection reference assumes a
completely known waveform, phase, synchronization, and channel in white proper
complex Gaussian noise. It is not a practical unknown-channel bit decoder or
an operational receiver planner.
"""
from __future__ import annotations

import argparse
import json
import math

from scipy.stats import norm


SECONDS_PER_DAY = 86400.0
ATTEMPT_SPACING_SECONDS = 60.0
MAX_FAMILY_ALTERNATIVES = 1_000_000_000


def _positive_finite(name: str, value: float) -> float:
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be positive and finite")
    return value


def _finite(name: str, value: float) -> float:
    value = float(value)
    if not math.isfinite(value):
        raise ValueError(f"{name} must be finite")
    return value


def _derived_finite(name: str, value: float, *, positive: bool = False) -> float:
    if not math.isfinite(value) or (positive and value <= 0):
        qualifier = "positive finite" if positive else "finite"
        raise ValueError(f"derived {name} is outside the supported {qualifier} range")
    return value


def _db_to_linear(name: str, decibels: float) -> float:
    """Convert a dB quantity while rejecting linear overflow and underflow."""
    try:
        value = 10.0 ** (decibels / 10.0)
    except OverflowError as exc:
        raise ValueError(f"derived {name} is outside the supported linear range") from exc
    return _derived_finite(name, value, positive=True)


def _count(name: str, value: int) -> int:
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not 1 <= value <= MAX_FAMILY_ALTERNATIVES):
        raise ValueError(f"{name} must be an integer in [1, {MAX_FAMILY_ALTERNATIVES}]")
    if int(value) != value or not 1 <= value <= MAX_FAMILY_ALTERNATIVES:
        raise ValueError(f"{name} must be an integer in [1, {MAX_FAMILY_ALTERNATIVES}]")
    return int(value)


def required_energy(detection_probability: float, total_alpha: float,
                    horizon_seconds: float, methods: int = 11,
                    durations: int = 4, bit_hypotheses: int = 2,
                    attempt_spacing_seconds: float = ATTEMPT_SPACING_SECONDS) -> dict:
    """Return the strict/relaxed family-adjusted known-waveform E/N0 reference."""
    methods = _count("methods", methods)
    durations = _count("durations", durations)
    bit_hypotheses = _count("bit hypotheses", bit_hypotheses)
    if not 0 < detection_probability < 1:
        raise ValueError("detection probability must be strictly between 0 and 1")
    if not 0 < total_alpha < 1:
        raise ValueError("total alpha must be strictly between 0 and 1")
    horizon_seconds = _positive_finite("horizon seconds", horizon_seconds)
    attempt_spacing_seconds = _positive_finite("attempt spacing seconds", attempt_spacing_seconds)
    ratio = _derived_finite("attempt count", horizon_seconds / attempt_spacing_seconds,
                            positive=True)
    if ratio > MAX_FAMILY_ALTERNATIVES:
        raise ValueError("attempt count exceeds supported family bound")
    attempts = math.ceil(ratio)
    p_false = (total_alpha * 0.5) / (attempts * methods * durations * bit_hypotheses)
    _derived_finite("per-hypothesis false tail", p_false, positive=True)
    z_false = float(norm.isf(p_false))
    z_detection = float(norm.ppf(detection_probability))
    rho = _derived_finite("required known-waveform E/N0", 0.5 * (z_false + z_detection) ** 2,
                          positive=True)
    achieved = float(norm.cdf(math.sqrt(2 * rho) - z_false))
    if abs(achieved - detection_probability) > 2e-14:
        raise ArithmeticError("known-waveform energy inversion self-check failed")
    return {
        "target_detection_probability": detection_probability,
        "total_false_admission_target": total_alpha,
        "score_alpha": total_alpha * 0.5,
        "horizon_seconds": horizon_seconds,
        "attempt_spacing_seconds": attempt_spacing_seconds,
        "scheduled_attempts": attempts,
        "method_alternatives_K": methods,
        "duration_alternatives_D": durations,
        "bit_hypotheses_N": bit_hypotheses,
        "per_hypothesis_false_tail": p_false,
        "known_waveform_Z_false_threshold": z_false,
        "known_waveform_Z_detection_quantile": z_detection,
        "required_known_waveform_E_N0_linear": rho,
        "required_known_waveform_E_N0_db": 10 * math.log10(rho),
        "inversion_check_detection_probability": achieved,
    }


def build_report(radiated_power_w: float = 1e-3, path_loss_db: float = 200.0,
                 noise_densities_dbm_hz=(-164.0, -154.0, -144.0),
                 days: float = 38.0, useful_energy_fraction: float = 1.0,
                 methods: int = 11, durations: int = 4,
                 bit_hypotheses: int = 2) -> dict:
    radiated_power_w = _positive_finite("radiated power watts", radiated_power_w)
    path_loss_db = _finite("path loss dB", path_loss_db)
    days = _positive_finite("days", days)
    if not math.isfinite(useful_energy_fraction) or not 0 < useful_energy_fraction <= 1:
        raise ValueError("useful energy fraction must be finite and in (0, 1]")
    noise_densities_dbm_hz = tuple(_finite("noise density dBm/Hz", n)
                                   for n in noise_densities_dbm_hz)
    if not noise_densities_dbm_hz:
        raise ValueError("provide at least one noise density")

    # Log-domain conversion avoids overflowing watts*1000 for finite powers.
    radiated_dbm = _derived_finite("radiated power dBm", 10.0 * math.log10(radiated_power_w) + 30.0)
    received_dbm = _derived_finite("received power dBm", radiated_dbm - path_loss_db)
    elapsed_seconds = _derived_finite("elapsed seconds", days * SECONDS_PER_DAY, positive=True)
    useful_seconds = _derived_finite("useful waveform seconds",
                                     elapsed_seconds * useful_energy_fraction, positive=True)
    targets = {
        "strict": required_energy(0.99, 1e-4, 86400.0, methods, durations, bit_hypotheses),
        "relaxed": required_energy(0.90, 1e-2, 43200.0, methods, durations, bit_hypotheses),
    }
    rows = []
    for noise_dbm_hz in noise_densities_dbm_hz:
        cn0_db_hz = _derived_finite("C/N0 dB-Hz", received_dbm - noise_dbm_hz)
        available_db = _derived_finite("nominal E/N0 dB",
                                       cn0_db_hz + 10.0 * math.log10(useful_seconds))
        available_linear = _db_to_linear("nominal E/N0", available_db)
        comparisons = {}
        for name, target in targets.items():
            required_linear = target["required_known_waveform_E_N0_linear"]
            ideal_pd = float(norm.cdf(math.sqrt(2.0 * available_linear) -
                                      target["known_waveform_Z_false_threshold"]))
            needed_useful_seconds = _db_to_linear("required useful waveform seconds",
                target["required_known_waveform_E_N0_db"] - cn0_db_hz)
            comparisons[name] = {
                "available_minus_required_known_waveform_energy_db":
                    available_db - target["required_known_waveform_E_N0_db"],
                "ideal_known_waveform_detection_probability": ideal_pd,
                "ideal_known_waveform_miss_probability": float(norm.sf(
                    math.sqrt(2.0 * available_linear) - target["known_waveform_Z_false_threshold"])),
                "required_useful_waveform_seconds": needed_useful_seconds,
                "required_elapsed_seconds_at_selected_energy_fraction":
                    _derived_finite("required elapsed seconds",
                        needed_useful_seconds / useful_energy_fraction, positive=True),
            }
        rows.append({
            "noise_density_dbm_hz": noise_dbm_hz,
            "received_power_dbm": received_dbm,
            "cn0_db_hz": cn0_db_hz,
            "nominal_energy_available_db": available_db,
            "nominal_energy_available_linear": available_linear,
            "comparisons": comparisons,
        })
    return {
        "schema": "triad-radiated-link-budget-v1",
        "inputs": {
            "radiated_power_w": radiated_power_w,
            "radiated_power_dbm": radiated_dbm,
            "path_loss_db": path_loss_db,
            "received_power_dbm": received_dbm,
            "elapsed_days": days,
            "elapsed_seconds": elapsed_seconds,
            "useful_energy_fraction": useful_energy_fraction,
            "useful_full_power_equivalent_seconds": useful_seconds,
            "methods_K": methods,
            "durations_D": durations,
            "bit_hypotheses_N": bit_hypotheses,
        },
        "targets": targets,
        "noise_sweep": rows,
        "assumptions": {
            "power": "Input is radiated power; no antenna efficiency factor is applied.",
            "energy": "C/N0 is received carrier power divided by supplied noise density; nominal E/N0 multiplies by useful full-power-equivalent waveform seconds.",
            "useful_energy_fraction": "A user-supplied fraction in (0,1] captures energy lost to outage, silence, shaping, or other effects; default 1 is an optimistic upper bound.",
            "noise": "Noise density is an explicit user input; no receiver noise temperature or noise figure is inferred.",
            "detector": "Detection probabilities and required energies are optimistic completely known-waveform, known-phase, known-sync, known-channel Gaussian references, not correct-bit performance for unknown keyed patterns or a practical decoder planner.",
            "family": "Strict: PD=.99, total alpha=1e-4, 86400-second horizon. Relaxed: PD=.90, total alpha=.01, 43200-second horizon. Half of alpha is allocated to scores; one attempt every 60 seconds, union bound across K methods, D durations, and N bit hypotheses.",
            "family_status": "Default K=11/D=4 is the expanded low-rate study family, including two proposed RBPF methods; it is not the earlier measured K=9/D=3 campaign. Counts remain configurable and must cover every selected alternative.",
        },
        "limitations": [
            "No claim of achievable unknown-channel correct-bit reception, operational feasibility, receiver sensitivity, or universal optimality.",
            "No propagation, antenna pattern, polarization, fading, synchronization, channel estimation, noise uncertainty, or receiver implementation loss model is included.",
            "Elapsed observation time is not automatically useful waveform time; the selected energy fraction must be justified independently.",
        ],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--radiated-power-w", type=float, default=1e-3)
    parser.add_argument("--path-loss-db", type=float, default=200.0)
    parser.add_argument("--noise-density-dbm-hz", type=float, action="append",
                        help="repeat for a noise-density sweep (default: -164, -154, -144)")
    parser.add_argument("--days", type=float, default=38.0)
    parser.add_argument("--useful-energy-fraction", type=float, default=1.0)
    parser.add_argument("--methods", type=int, default=11)
    parser.add_argument("--durations", type=int, default=4)
    parser.add_argument("--bit-hypotheses", type=int, default=2)
    parser.add_argument("--output", type=argparse.FileType("w"), default="-")
    args = parser.parse_args(argv)
    try:
        report = build_report(args.radiated_power_w, args.path_loss_db,
            args.noise_density_dbm_hz if args.noise_density_dbm_hz is not None
            else (-164.0, -154.0, -144.0), args.days, args.useful_energy_fraction,
            args.methods, args.durations, args.bit_hypotheses)
    except ValueError as exc:
        parser.error(str(exc))
    json.dump(report, args.output, indent=2, sort_keys=True, allow_nan=False)
    args.output.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
