#!/usr/bin/env python3
"""Reproduce analytical known-waveform energy references without raw exports.

Run with Python + SciPy from any directory. The default JSON is written beside
this script. No local-evidence paths or simulator exports are required. Optional
actual-energy inputs must be retained-energy diagnostic JSON archived beside
this script (or below it), and are bound by SHA256 in the output.

For a completely known deterministic complex waveform s in white proper complex
Gaussian noise n~CN(0,N0 I), rho=||s||²/N0 and
Z=sqrt(2)*Re(s.H@y)/(sqrt(N0)*||s||) is N(0,1) under H0 and
N(sqrt(2*rho),1) under H1. A threshold z_FA=Phi^-1(1-p_FA) therefore gives
P_D=Phi(sqrt(2*rho)-z_FA). Solving for target P_D gives
rho_required=[Q^-1(p_FA)+Phi^-1(P_D)]²/2.

This is a coherent known-waveform/known-phase signal-versus-noise benchmark.
It is NOT correct-bit reception with unknown keyed patterns, synchronization,
channel, phase or noise power. The separately reported Markov LR gate belongs
to the study's normalized-unknown-model receivers; it is not the Gaussian Z
threshold used to derive this optimistic energy reference.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform

import scipy
from scipy.stats import norm


HERE = Path(__file__).resolve().parent
DEFAULT_OUTPUT = HERE/"known-channel-benchmarks.json"
CN0_GRID = (-4., -16., -24., -34.)
USEFUL_SECONDS_GRID = (120., 3600., 172800.)
METHODS, DURATIONS, BIT_HYPOTHESES = 9, 3, 2
CALIBRATION_ALTERNATIVES, ATTEMPT_SPACING_SECONDS = 2, 60.


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def family(name, detection_probability, total_alpha, horizon_seconds):
    attempts = math.ceil(horizon_seconds/ATTEMPT_SPACING_SECONDS)
    score_alpha = total_alpha*.5
    p_false = score_alpha/(attempts*METHODS*DURATIONS*BIT_HYPOTHESES)
    z_false = float(norm.isf(p_false))
    z_detection = float(norm.ppf(detection_probability))
    required = .5*(z_false+z_detection)**2
    # Algebraic inversion checked independently against the normal CDF.
    achieved = float(norm.cdf(math.sqrt(2*required)-z_false))
    if abs(achieved-detection_probability) > 2e-14:
        raise ArithmeticError("known-waveform energy inversion self-check failed")
    return {"name": name, "known_waveform_detection_probability_target": detection_probability,
        "total_false_admission_target": total_alpha, "horizon_seconds": horizon_seconds,
        "attempt_spacing_seconds": ATTEMPT_SPACING_SECONDS, "scheduled_attempts": attempts,
        "method_alternatives_K": METHODS, "duration_alternatives_D": DURATIONS,
        "bit_hypotheses_N": BIT_HYPOTHESES,
        "score_alpha": score_alpha, "calibration_alpha_reserved": total_alpha-score_alpha,
        "calibration_alternatives": CALIBRATION_ALTERNATIVES,
        "calibration_allocation_note": "N=2 shares the calibration half-budget; it does not multiply the score family a second time",
        "per_hypothesis_false_tail": p_false, "known_waveform_Z_false_threshold": z_false,
        "known_waveform_Z_detection_quantile": z_detection,
        "required_known_waveform_E_N0_linear": required,
        "required_known_waveform_E_N0_db": 10*math.log10(required),
        "study_markov_log_LR_gate_separate_from_Z_detector": math.log(1/p_false),
        "known_waveform_detection_probability_inversion_check": achieved}


def energy_comparison(energy_linear, targets):
    if not math.isfinite(energy_linear) or energy_linear <= 0:
        raise ValueError("comparison energy must be positive finite")
    energy_db = 10*math.log10(energy_linear)
    result = {"E_N0_linear": energy_linear, "E_N0_db": energy_db}
    for name, target in targets.items():
        argument = math.sqrt(2*energy_linear)-target["known_waveform_Z_false_threshold"]
        result[name] = {
            "available_minus_required_known_waveform_energy_db":
                energy_db-target["required_known_waveform_E_N0_db"],
            "ideal_known_waveform_detection_probability": float(norm.cdf(argument)),
            "ideal_known_waveform_miss_probability": float(norm.sf(argument))}
    return result


def archived_energy_records(paths, targets):
    """Optional comparisons use only an archived diagnostic's numeric record."""
    result = []
    for supplied in paths:
        path = (HERE/supplied).resolve()
        if not path.is_relative_to(HERE):
            raise ValueError("energy diagnostics must be archived beside/below this benchmark script")
        data = json.loads(path.read_text())
        if data.get("schema") != "triad-retained-energy-v1":
            raise ValueError("optional energy input must be a retained-energy diagnostic")
        records = []
        for index, row in enumerate(data.get("paths", [])):
            if row.get("status") != "completed":
                continue
            energy = float(row["total_signal_energy_over_noise"])
            records.append({"diagnostic_record_index": index,
                "sampled_observation_seconds": row.get("sampled_observation_seconds"),
                "valid_complex_samples": row.get("valid_complex_samples"),
                "signal_energy_samples_simulation_truth_only": row.get("total_signal_energy_samples"),
                "comparison": energy_comparison(energy, targets)})
        result.append({"archived_diagnostic": path.relative_to(HERE).as_posix(),
            "sha256": sha256(path), "diagnostic_source_sha256": data.get("source_sha256"),
            "records": records,
            "scope": "reproduced from archived aggregate simulator-truth energy diagnostics; original raw exports are not required by this calculation"})
    return result


def build_report(energy_diagnostics=()):
    targets = {"strict": family("strict", .99, 1e-4, 86400.),
               "relaxed": family("relaxed", .90, .01, 43200.)}
    rows = []
    for cn0 in CN0_GRID:
        for seconds in USEFUL_SECONDS_GRID:
            power = 10**(cn0/10)
            rows.append({"cn0_db_hz": cn0,
                "nominal_useful_full_power_signal_seconds": seconds,
                **energy_comparison(power*seconds, targets),
                "required_nominal_useful_signal_seconds": {
                    name: target["required_known_waveform_E_N0_linear"]/power
                    for name, target in targets.items()}})
    selected = [row for row in rows if
        (row["cn0_db_hz"], row["nominal_useful_full_power_signal_seconds"]) in
        ((-16., 3600.), (-34., 172800.))]
    return {"schema": "triad-known-channel-benchmarks-v2",
        "script_sha256": sha256(__file__),
        "reproduce": "python known_channel_benchmarks.py; requires Python and SciPy, no local exports",
        "software": {"python": platform.python_version(), "scipy": scipy.__version__},
        "assumptions": {
            "signal_model": "completely known deterministic complex waveform, phase, synchronization and channel in known white proper complex Gaussian noise",
            "energy": "total received triplet power allocated to one bit, multiplied by useful full-power time; E/N0=(C/N0 linear)*T",
            "nominal_time": "no shaping, outage, fading, burst-silence or multiple-bit energy allocation loss; elapsed observation time is not automatically useful signal time",
            "family": "union bound over K=9 methods, D=3 durations, N=2 bit alternatives and one scheduled attempt every60seconds",
            "score_allocation": "half total alpha is reserved for scores, half for calibration; N=2 calibration policies share the latter half",
            "formula": "rho_required=0.5*(Qinv(p_false_per_hypothesis)+Phiinv(P_D))**2",
            "detection_probability_formula": "P_D=Phi(sqrt(2*rho)-Qinv(p_false_per_hypothesis))",
            "gate_distinction": "known-waveform Gaussian tail threshold is optimized for this detector; the study Markov log-LR gate is reported separately"},
        "targets": targets, "headline_nominal_cases": selected, "nominal_grid": rows,
        "archived_actual_energy_comparisons": archived_energy_records(energy_diagnostics, targets),
        "limitations": [
            "Known-waveform signal-versus-noise detection is not correct reception of an unknown keyed bit and does not model legitimate nuisance transformations or codeword separation.",
            "Unknown synchronization, phase/channel evolution, noise calibration, retained-projection losses and computational limits can require additional energy.",
            "Nominal energy does not replace archived actual post-erasure signal energy; this default report includes no actual-energy claim.",
            "The family union bound is conservative for correlated alternatives and is fixed-window accounting, not unrestricted sequential stopping.",
            "A probability rounded to1 or a miss probability underflowed to0 is floating-point output, not a mathematical certainty.",
            "No measured receiver gain, universal optimum, physical duration qualification or SNR-wall claim follows from these analytical references."]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--energy-diagnostic", action="append", default=[],
                        help="optional archived retained-energy JSON, relative to this script's directory")
    args = parser.parse_args(argv)
    report = build_report(args.energy_diagnostic)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+"\n")
    print(args.output)


if __name__ == "__main__":
    main()
