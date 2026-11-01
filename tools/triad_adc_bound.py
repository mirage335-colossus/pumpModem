#!/usr/bin/env python3
"""Sample-domain ADC rounding with a conservative fixed-family admission bound.

For a complex observation of N samples rounded independently in real/imaginary
components with step delta, an unclipped capture has ||e|| <= delta*sqrt(N/2).
For any nonzero fixed template q, Cauchy--Schwarz and the reverse triangle
inequality give

  |q^H y_analog|/(sigma*||q||)
    >= |q^H y_ADC|/(sigma*||q||) - delta*sqrt(N/2)/sigma.

Under analog H0 CN(0,sigma^2 I), the squared normalized analog projection is
Exp(1). Therefore requiring the ADC projection amplitude to exceed sqrt(g)
plus this error margin, with g=log(family/alpha), gives an ideal-arithmetic
union bound alpha over the full declared finite family. No independence of
candidate scores or acquisition attempts is assumed. Any ADC rail code causes
capture rejection. This observable rule catches every clipped capture, plus
some unclipped rail hits; the rounding bound does not cover saturation errors.

This is a fixed rank-one, known-window sample-domain experiment. Floating point
roundoff is measured but not formally certified. Interference, unknown/estimated
noise covariance, AGC, dither, RF retuning and operational full acquisition are
outside this bound. The ordinary receiver is unchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import platform
import sys
import time

import numpy as np
from scipy.stats import beta


TARGETS = {"operational_default": (.99, 1e-4, 86400.),
           "relaxed": (.90, .01, 43200.)}
SPLITS = ("development", "calibration", "holdout")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024*1024), b""):
            digest.update(block)
    return digest.hexdigest()


def seed_for(root: int, split: str, trial: int) -> int:
    if root < 0 or split not in SPLITS or trial < 0:
        raise ValueError("nonnegative root/trial and declared split required")
    encoded = json.dumps(["triad-adc-bound-v1", root, split, trial],
                         separators=(",", ":")).encode("ascii")
    return int.from_bytes(hashlib.sha256(encoded).digest()[:16], "little")


def quantize(observations: np.ndarray, bits: int, full_scale: float):
    """Return rounded values, observable rail counts, step, and true clip counts.

    delta=2*full_scale/2**bits; codes are [-2**(bits-1),2**(bits-1)-1].
    NumPy rint uses ties-to-even; the native C++ frontend uses ties-away.
    Either convention obeys the same half-step bound. This independent ADC
    variant is not a bit-for-bit clone of the native converter.
    True overload means the nearest integer code is outside that interval.
    Admission uses only output rail detection, never the simulator's privileged
    pre-saturation flag. Every true overload produces a rail code.
    """
    if (isinstance(bits, bool) or not 2 <= bits <= 24 or
            not math.isfinite(full_scale) or full_scale <= 0):
        raise ValueError("bits in [2,24] and finite positive full scale required")
    x = np.asarray(observations, dtype=np.complex128)
    if x.ndim != 2 or not np.isfinite(x).all():
        raise ValueError("finite [capture,sample] observations required")
    step = math.ldexp(2*full_scale, -bits)
    if not math.isfinite(step) or step <= 0:
        raise ValueError("unrepresentable quantizer step")
    low, high = -(2**(bits-1)), 2**(bits-1)-1
    real, imag = np.rint(x.real/step), np.rint(x.imag/step)
    clipped_components = np.count_nonzero((real < low) | (real > high), axis=1)
    clipped_components += np.count_nonzero((imag < low) | (imag > high), axis=1)
    rail_components = np.count_nonzero((real <= low) | (real >= high), axis=1)
    rail_components += np.count_nonzero((imag <= low) | (imag >= high), axis=1)
    rounded = np.clip(real, low, high)*step + 1j*np.clip(imag, low, high)*step
    return rounded, rail_components, step, clipped_components


def rounding_norm_bound(samples: int, step: float) -> float:
    if samples < 1 or not math.isfinite(step) or step <= 0:
        raise ValueError("positive sample count and finite positive step required")
    # One upward representable step is a convenience guard, not a proof of
    # the complete FP64 projection/normalization arithmetic.
    return float(np.nextafter(step*math.sqrt(samples/2.), math.inf))


def normalized_amplitudes(templates: np.ndarray, observations: np.ndarray,
                          variance: float) -> np.ndarray:
    """Independent NumPy complex FP64 sample-domain correlations."""
    q = np.asarray(templates, dtype=np.complex128)
    y = np.asarray(observations, dtype=np.complex128)
    if q.ndim != 2 or y.ndim != 2 or q.shape[1] != y.shape[1]:
        raise ValueError("templates/captures must have the same nonempty sample axis")
    norms = np.linalg.norm(q, axis=1)
    if (not q.shape[1] or not np.isfinite(q).all() or not np.isfinite(y).all() or
            not np.isfinite(norms).all() or np.any(norms <= 0) or
            not math.isfinite(variance) or variance <= 0):
        raise ValueError("finite nonzero templates and positive variance required")
    return np.abs(y @ q.conj().T)/(math.sqrt(variance)*norms[None, :])


def gate(samples: int, step: float, variance: float, candidates: int,
         alpha: float, horizon_seconds: float, attempt_spacing: float,
         duration_alternatives: int = 1):
    if (not math.isfinite(variance) or variance <= 0 or candidates < 1 or
            not 0 < alpha < 1 or horizon_seconds <= 0 or attempt_spacing <= 0 or
            duration_alternatives < 1):
        raise ValueError("valid positive variance, family and target required")
    attempts = math.ceil(horizon_seconds/attempt_spacing)
    family = candidates*attempts*duration_alternatives
    ideal_energy = math.log(family/alpha)
    margin = rounding_norm_bound(samples, step)/math.sqrt(variance)
    threshold = float(np.nextafter(math.sqrt(ideal_energy)+margin, math.inf))
    return {"attempts_per_horizon": attempts, "candidate_tests_per_attempt": candidates,
            "duration_alternatives": duration_alternatives,
            "full_declared_family_tests": family, "false_probability_per_horizon": alpha,
            "horizon_seconds": horizon_seconds, "attempt_spacing_seconds": attempt_spacing,
            "ideal_analog_energy_threshold": ideal_energy,
            "rounding_error_norm_bound": rounding_norm_bound(samples, step),
            "normalized_amplitude_margin": margin,
            "quantized_amplitude_threshold": threshold,
            "gate_energy_inflation_db": 20*math.log10(threshold/math.sqrt(ideal_energy)),
            "inflation_interpretation": "deterministic gate inflation; not a measured receiver sensitivity loss"}


def cp95(successes, total):
    return [0. if not successes else float(beta.ppf(.025, successes, total-successes+1)),
            1. if successes == total else float(beta.ppf(.975, successes+1, total-successes))]


def load_bank(path: Path):
    source = Path(__file__).with_name("triad-gpu-study.py")
    spec = importlib.util.spec_from_file_location("triad_adc_bank_validator", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    directory, manifest = module._load_manifest(path)
    data = module._validate_manifest(directory, manifest)
    if (manifest.get("gaussian_projection_supported") is False or
            manifest.get("analytic_admission_supported") is False or
            manifest.get("sampled_frontend", {}).get("active", False)):
        raise ValueError("source export is already outside this ideal analog Gaussian model")
    c, n, k = data["basis_shape"]
    if k != 1 or data["signal_bit"] is None or set(data["bitlabels"]) != {0, 1}:
        raise ValueError("fixed rank-one bank with both bits and signal truth required")
    q = np.asarray(data["basis"].numpy(), dtype=np.complex128)[:, :, 0].copy()
    q /= np.linalg.norm(q, axis=1)[:, None]
    signal = np.asarray(data["signal"].numpy(), dtype=np.complex128).copy()
    files = [directory/"manifest.json", directory/manifest.get("basis_file", "basis.c64"),
             directory/manifest.get("signal_file", "signal.c64")]
    return data, q, signal, files, source


def run(args):
    start = time.perf_counter()
    if args.output.exists():
        raise ValueError("output must be fresh")
    data, q, signal, input_files, validator = load_bank(args.input)
    c, n = q.shape
    estimate = args.batch*n*16*20 + q.nbytes*4
    if estimate > args.memory_mib*1024**2:
        raise ValueError(f"estimated peak {estimate} exceeds memory cap")
    preparation_seconds = time.perf_counter()-start
    stages = {"preparation": preparation_seconds, "sample_rng": 0., "analog_projection": 0.,
              "adc_rounding": 0., "quantized_projection": 0., "verification": 0.}
    rows = []
    max_observed_rounding_bound_ratio = 0.
    bound_failure_count = 0
    for cn0 in args.cn0_grid:
        variance = data["noise_reference_power"]*data["sample_rate"]/10**(cn0/10)
        records = {(bits, fs, target): {"signal_correct": 0, "signal_wrong": 0,
                   "signal_missed": 0, "null_admitted": 0, "signal_overloaded": 0,
                   "null_overloaded": 0, "signal_clipped_components": 0,
                   "signal_rail_rejected": 0, "null_rail_rejected": 0,
                   "null_clipped_components": 0, "bound_implies_analog_failures": 0,
                   "max_abs_normalized_amplitude_change_unclipped": 0.,
                   "max_amplitude_change_over_deterministic_margin_unclipped": 0.,
                   "ideal_signal_correct": 0, "ideal_signal_wrong": 0,
                   "ideal_null_admitted": 0} for bits in args.bits for fs in args.full_scale
                   for target in TARGETS}
        for first in range(0, args.trials, args.batch):
            count = min(args.batch, args.trials-first)
            t = time.perf_counter()
            noise = []
            for trial in range(first, first+count):
                rng = np.random.default_rng(seed_for(args.seed, args.split, trial))
                noise.append((rng.standard_normal(n)+1j*rng.standard_normal(n))*math.sqrt(variance/2))
            null = np.stack(noise)
            positive = null+signal[None, :]
            stages["sample_rng"] += time.perf_counter()-t
            t = time.perf_counter()
            original = {"null": normalized_amplitudes(q, null, variance),
                        "signal": normalized_amplitudes(q, positive, variance)}
            stages["analog_projection"] += time.perf_counter()-t
            for bits in args.bits:
                for fs in args.full_scale:
                    t = time.perf_counter()
                    rounded = {label: quantize(obs, bits, fs) for label, obs in
                               (("null", null), ("signal", positive))}
                    stages["adc_rounding"] += time.perf_counter()-t
                    t = time.perf_counter()
                    amplitudes = {label: normalized_amplitudes(q, value[0], variance)
                                  for label, value in rounded.items()}
                    stages["quantized_projection"] += time.perf_counter()-t
                    t = time.perf_counter()
                    for label, obs in (("null", null), ("signal", positive)):
                        quantized, rail_counts, step, true_clip_counts = rounded[label]
                        valid = rail_counts == 0
                        epsilon = rounding_norm_bound(n, step)
                        if valid.any():
                            error_norm = np.linalg.norm(quantized[valid]-obs[valid], axis=1)
                            ratio = float(error_norm.max()/epsilon)
                            max_observed_rounding_bound_ratio = max(max_observed_rounding_bound_ratio, ratio)
                            bound_failure_count += int(np.count_nonzero(error_norm > epsilon*(1+1e-12)))
                        for name, (_, alpha, horizon) in TARGETS.items():
                            g = gate(n, step, variance, c, alpha, horizon,
                                     args.attempt_spacing, args.duration_alternatives)
                            score, analog = amplitudes[label], original[label]
                            admitted_candidates = (score >= g["quantized_amplitude_threshold"]) & valid[:, None]
                            admitted = admitted_candidates.any(1)
                            # Every admitted quantized candidate, including a wrong-bit
                            # one, must pass that SAME candidate's analog threshold.
                            failures = admitted_candidates & (analog < math.sqrt(g["ideal_analog_energy_threshold"]))
                            record = records[bits, fs, name]
                            if valid.any():
                                change = float(np.max(np.abs(score[valid]-analog[valid])))
                                record["max_abs_normalized_amplitude_change_unclipped"] = max(
                                    record["max_abs_normalized_amplitude_change_unclipped"], change)
                                record["max_amplitude_change_over_deterministic_margin_unclipped"] = max(
                                    record["max_amplitude_change_over_deterministic_margin_unclipped"],
                                    change/g["normalized_amplitude_margin"])
                            record["bound_implies_analog_failures"] += int(failures.sum())
                            record[label+"_overloaded"] += int(np.count_nonzero(true_clip_counts))
                            record[label+"_clipped_components"] += int(true_clip_counts.sum())
                            record[label+"_rail_rejected"] += int(np.count_nonzero(~valid))
                            labels = np.asarray(data["bitlabels"])
                            ideal_admitted = analog.max(1) >= math.sqrt(g["ideal_analog_energy_threshold"])
                            if label == "null":
                                record["null_admitted"] += int(admitted.sum())
                                record["ideal_null_admitted"] += int(ideal_admitted.sum())
                            else:
                                winner = labels[score.argmax(1)]
                                correct = admitted & (winner == data["signal_bit"])
                                wrong = admitted & (winner != data["signal_bit"])
                                record["signal_correct"] += int(correct.sum())
                                record["signal_wrong"] += int(wrong.sum())
                                record["signal_missed"] += int((~admitted).sum())
                                ideal_winner = labels[analog.argmax(1)]
                                record["ideal_signal_correct"] += int((ideal_admitted & (ideal_winner == data["signal_bit"])).sum())
                                record["ideal_signal_wrong"] += int((ideal_admitted & (ideal_winner != data["signal_bit"])).sum())
                    stages["verification"] += time.perf_counter()-t
        for (bits, fs, name), counts in records.items():
            _, alpha, horizon = TARGETS[name]
            step = math.ldexp(2*fs, -bits)
            rows.append({"cn0_db_hz": cn0, "target": name, "bits": bits, "full_scale": fs,
                         "step": step, "complex_noise_variance": variance, "trials": args.trials,
                         "counts": counts, "signal_correct_95pct_cp_ci": cp95(counts["signal_correct"], args.trials),
                         "null_admitted_95pct_cp_ci": cp95(counts["null_admitted"], args.trials),
                         "gate": gate(n, step, variance, c, alpha, horizon,
                                      args.attempt_spacing, args.duration_alternatives)})
    result = {"schema": "triad-adc-admission-bound-v1", "status": "conditional_bound_screening",
        "command_argv": sys.argv, "configuration": {k: str(v) if isinstance(v, Path) else v for k,v in vars(args).items()},
        "environment": {"python": platform.python_version(), "numpy": np.__version__,
                        "scipy": __import__("scipy").__version__, "device": "CPU FP64",
                        "thread_scope": "set OPENBLAS_NUM_THREADS/OMP_NUM_THREADS externally; no GPU"},
        "inputs_sha256": {str(p): sha256(p) for p in input_files},
        "source_sha256": {str(p): sha256(p) for p in (Path(__file__), validator)},
        "seed_recipe": "SHA256 JSON [triad-adc-bound-v1, root, split, trial], first128bits little-endian; same standard-normal physical noise reused across SNR and ADC cases, H0/H1 paired",
        "reference": "independent NumPy complex128 direct sampled correlations; FP64 normalization of fixed exported complex64 templates; no independent waveform generator",
        "workload": {"candidates": c, "samples": n, "rank_per_candidate": 1,
                     "candidateparams": data["candidateparams"], "sample_rate": data["sample_rate"],
                     "sampled_h0": "independent circular complex Gaussian analog samples; no filtering/interference/AGC/ENOB/dither added",
                     "full_family_accounting": "all exported candidates and declared attempt/duration counts; one attempt is sampled in each trial, repeated-attempt union bound is analytic"},
        "quantizer": {"nominal_code_bits": args.bits,
                      "rounding": "nearest, ties-to-even (NumPy rint)",
                      "step": "2*full_scale/2**bits",
                      "code_range": "[-2**(bits-1),2**(bits-1)-1]",
                      "overload_counter": "nearest unbounded integer code outside code range",
                      "admission_rejection": "any output component equals either digital rail",
                      "native_frontend_difference": "C++ uses ties-away and counts analog range excursions before rounding; this independent variant has the same rounding-error bound on accepted captures. Continuous Gaussian ties have probability zero in the mathematical model, but finite numerical samples can differ."},
        "bound": {"proof": __doc__, "ideal_arithmetic_union_bound_valid_under_model": True,
                  "floating_point_certified": False, "operationally_qualified": False,
                  "admission_uses_only_quantized_rail_codes": True,
                  "observed_rounding_norm_bound_failures": bound_failure_count,
                  "max_observed_error_norm_over_bound_unclipped": max_observed_rounding_bound_ratio,
                  "admitted_candidate_implies_analog_failure_count": sum(r["counts"]["bound_implies_analog_failures"] for r in rows)},
        "timing_seconds": {**stages,
            "complete_driver_wall_including_hashing_excluding_final_json_write": time.perf_counter()-start},
        "estimated_peak_bytes": estimate, "results": rows,
        "limitations": ["This conservative detector rejects any capture containing an ADC rail code, including every overload; it does not use the simulator's privileged true-clipping flags. It is an experimental rule, not a change to the default modem.",
            "The proof assumes fixed templates and known circular white Gaussian analog noise variance. Nonlinear observations are actually quantized sample by sample.",
            "The ideal-arithmetic proof is not a floating-point error certificate; templates and scores are FP64 numerical approximations.",
            "Zero null events do not establish the tiny target empirically. The bound comes from triangle inequality plus the analog Gaussian tail and union bound.",
            "Different SNR and ADC cases reuse the same noise and waveform. Pointwise confidence intervals must not be pooled.",
            "Gate inflation is not measured dB sensitivity loss. These fixed-path numerical trials do not span changing physical paths or full timing/CFO/epoch acquisition."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--trials", type=int, default=256)
    p.add_argument("--batch", type=int, default=16)
    p.add_argument("--seed", type=int, default=195071)
    p.add_argument("--split", choices=SPLITS, default="development")
    p.add_argument("--cn0-grid", default="0,0.5,1,1.5,2")
    p.add_argument("--bits", type=int, nargs="+", default=[16])
    p.add_argument("--full-scale", type=float, nargs="+", default=[32., 512., 65536.])
    p.add_argument("--attempt-spacing", type=float, default=60.)
    p.add_argument("--duration-alternatives", type=int, default=1)
    p.add_argument("--memory-mib", type=int, default=512)
    a = p.parse_args()
    try:
        a.cn0_grid = [float(v) for v in a.cn0_grid.split(",")]
        if (not 1 <= a.trials <= 100000 or not 1 <= a.batch <= 1024 or a.seed < 0 or
                a.memory_mib < 1 or a.duration_alternatives < 1 or
                not math.isfinite(a.attempt_spacing) or a.attempt_spacing <= 0 or
                not a.cn0_grid or any(not math.isfinite(x) or abs(x) > 150 for x in a.cn0_grid) or
                not a.bits or any(not 2 <= b <= 24 for b in a.bits) or len(set(a.bits)) != len(a.bits) or
                not a.full_scale or any(not math.isfinite(f) or f <= 0 for f in a.full_scale) or
                len(set(a.full_scale)) != len(a.full_scale)):
            raise ValueError("invalid trial/batch, finite grid, quantizer, family or memory parameters")
        result = run(a)
    except (ValueError, OverflowError) as error:
        p.error(str(error))
    print(json.dumps({"output": str(a.output), "rows": len(result["results"]),
                      "status": result["status"], "bound": result["bound"]}, default=str))


if __name__ == "__main__":
    main()
