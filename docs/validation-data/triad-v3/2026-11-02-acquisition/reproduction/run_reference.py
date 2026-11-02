#!/usr/bin/env python3
"""Reproduce a small matched Gaussian covariance acquisition reference.

This is a diagnostic for one retained public sampled basis, not an end-to-end
receiver or a model of the finite Each-65 selective channel.  The basis rows
0/1 are the one-tap bit alternatives; rows 2/3 are the matching two-ray
alternatives.  The reported screen uses the two-ray rows and an OFF-selective
proper-complex Gaussian coefficient prior.  See the module docstring in
``tools/triad_covariance_reference.py`` for the exact covariance law.

Examples (from repository root):
  OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 \
    docs/validation-data/triad-v3/2026-11-02-acquisition/reproduction/run_reference.py \
    --pilot --output local-evidence/triad-acquisition-improvement/reference/model-screen/pilot.json
  OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 \
    docs/validation-data/triad-v3/2026-11-02-acquisition/reproduction/run_reference.py \
    --output local-evidence/triad-acquisition-improvement/reference/model-screen/full.json

The script never generates or alters sampled waveform captures. It reuses the
provided basis, fixes a 256-sample window over indices [1024,1280) (samples
1024 through 1279; timestamps 0.5000000 through 0.6245117 seconds at 2048 Hz,
with 0.125 seconds of sample-slot duration), normalizes each candidate/tap
column to unit mean power, and assigns tap powers (1/2, 1/2). Each CN0 uses
noise variance Fs / 10^(CN0/10).
"block-center" models are fixed-window approximations with independent
windows, not independent blocks over the whole long-duration waveform.
"FP32-bulk" changes only the whitening arithmetic; covariance construction,
factorization, log determinants and evidence reductions remain FP64.
"Classification" is bit-vs-bit only: no admission threshold or false alarm
probability is measured here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time

import numpy as np
import scipy
from scipy.special import ndtr


ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT))


def _load_reference():
    """Load the exact reference implementation and its SciPy dependencies."""
    import tools.triad_covariance_reference as ref
    return ref


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def wilson(errors: int, trials: int, z: float = 1.959963984540054):
    p = errors / trials
    den = 1 + z*z/trials
    center = (p + z*z/(2*trials)) / den
    half = z * math.sqrt(p*(1-p)/trials + z*z/(4*trials*trials)) / den
    return [max(0.0, center-half), min(1.0, center+half)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--basis-dir", type=Path, default=Path(
        "local-evidence/triad-acquisition-improvement/reference/public-basis"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--trials", type=int, default=2048)
    parser.add_argument("--pilot", action="store_true", help="2 CN0 values, 256 trials per truth bit")
    parser.add_argument("--validation-grid", action="store_true", help="independent 4096-trial fine C/N0 grid for 90/99%% bit accuracy screening")
    parser.add_argument("--seed", type=int)
    args = parser.parse_args()
    if args.pilot and args.validation_grid:
        parser.error("--pilot and --validation-grid are mutually exclusive")
    if args.validation_grid:
        args.trials = 4096
        cn0_values = [20.0, 20.5, 21.0, 21.5, 22.0, 24.0, 25.0, 26.0, 27.0, 28.0]
    elif args.pilot:
        args.trials = 256
        cn0_values = [8.0, 28.0]
    else:
        cn0_values = [8.0, 12.0, 16.0, 20.0, 24.0, 28.0]
    if args.seed is None:
        args.seed = 2026110202 if args.validation_grid else 2026110201
    if not 1 <= args.trials <= (4096 if args.validation_grid else 2048):
        parser.error("--trials exceeds the selected bounded study limit")
    basis_dir = args.basis_dir.resolve()
    output = args.output.resolve()
    manifest_path = basis_dir / "manifest.json"
    basis_path = basis_dir / "basis.c128"
    manifest = json.loads(manifest_path.read_text())
    if manifest["basis_shape"] != [4, 19199, 2] or manifest["sample_rate"] != 2048:
        parser.error("provided sampled basis does not match the fixed study fixture")
    ref = _load_reference()

    began = time.perf_counter()
    raw = np.fromfile(basis_path, dtype="<c16").reshape(manifest["basis_shape"])
    start, stop = 1024, 1280
    basis = raw[[2, 3], start:stop, :].copy()
    # The input basis is a sample waveform with carrier and pattern structure.
    # Unit tap-column normalization states a clean equal-average-power channel
    # prior while leaving phase, shaping and candidate distinction intact.
    tap_norms = np.mean(np.abs(basis)**2, axis=1)
    if not np.isfinite(tap_norms).all() or np.any(tap_norms <= 0):
        raise ValueError("basis tap has zero or invalid energy in the chosen window")
    basis /= np.sqrt(tap_norms[:, None, :])
    sample_rate = float(manifest["sample_rate"])
    sample_times = np.arange(start, stop, dtype=np.float64) / sample_rate
    models = {"exact": (None, None)}
    for block in (32, 16, 8):
        models[f"block{block}"] = (block, 256)
    covariances = {}
    for name, (block, window) in models.items():
        kwargs = {} if block is None else {"block_samples": block, "window_samples": window}
        covariances[name] = ref.channel_covariances(
            basis, sample_times, 30.0, (0.5, 0.5), **kwargs)

    # Conditional known-realized-channel benchmark: draw shared tap paths from
    # the same exact SE prior, compare both candidate mean waveforms under each
    # realization, and evaluate the equal-prior AWGN Q bound. This is a
    # classification lower bound, not signal admission or a receiver result.
    coefficient_covariance = ref.gaussian_coefficient_covariance(sample_times, 30.0)
    eigenvalues, eigenvectors = np.linalg.eigh(coefficient_covariance)
    eig_tolerance = 1e-12 * float(eigenvalues[-1])
    if eigenvalues[0] < -eig_tolerance:
        raise ValueError("SE coefficient covariance has a material negative eigenvalue")
    # The 256-point, oversampled SE matrix is numerically rank-deficient:
    # eigensolver roundoff gives tiny negative modes (~1e-14 versus max ~53).
    # Clamp only modes within the stated relative tolerance; this represents
    # the same PSD covariance to FP64 precision without a diagonal jitter.
    coefficient_root = eigenvectors * np.sqrt(np.maximum(eigenvalues, 0.0))[None, :]
    channel_rng = np.random.default_rng(args.seed + 71)
    z = (channel_rng.standard_normal((args.trials, 256, 2)) +
         1j * channel_rng.standard_normal((args.trials, 256, 2))) / math.sqrt(2.0)
    channel_coefficients = np.einsum("ij,tjk->tik", coefficient_root, z, optimize=True)
    realized_means = np.stack([
        sum(math.sqrt(power) * basis[bit, :, tap][None, :] * channel_coefficients[:, :, tap]
            for tap, power in enumerate((0.5, 0.5))) for bit in (0, 1)], axis=1)
    known_channel_distance2 = np.sum(np.abs(realized_means[:, 0] - realized_means[:, 1])**2, axis=1)
    # Antipodal scalar sanity check: for distance d in CN(0,sigma^2),
    # Pe=Q(d/sqrt(2 sigma^2))=ndtr(-d/sqrt(2 sigma^2)).
    antipodal_distance = 2.0
    antipodal_noise_variance = 0.5
    antipodal_bound = ref.known_realized_channel_error_probability(
        np.array([0.0j]), np.array([antipodal_distance + 0.0j]),
        antipodal_noise_variance)
    antipodal_expected = float(ndtr(-antipodal_distance /
                                    math.sqrt(2.0 * antipodal_noise_variance)))
    if not math.isclose(antipodal_bound, antipodal_expected, rel_tol=1e-14, abs_tol=0.0):
        raise AssertionError("known-channel Gaussian antipodal formula disagrees with accepted helper")

    results = []
    rng = np.random.default_rng(args.seed)
    # Consume one seed per truth bit and CN0, then reuse the resulting exact
    # observation matrix for all approximations and arithmetic modes.
    for cn0 in cn0_values:
        noise_variance = sample_rate / (10.0 ** (cn0 / 10.0))
        known_channel_pe = ndtr(-np.sqrt(known_channel_distance2 /
                                         (2.0 * noise_variance)))
        helper_check = ref.known_realized_channel_error_probability(
            realized_means[0, 0], realized_means[0, 1], noise_variance)
        if not math.isclose(float(known_channel_pe[0]), helper_check,
                            rel_tol=1e-12, abs_tol=0.0):
            raise AssertionError("vector known-channel Q values disagree with accepted scalar helper")
        factors = {name: [ref.factor_covariance(K[b], noise_variance)
                          for b in range(2)]
                   for name, K in covariances.items()}
        distances = {"exact": {
            "kl_0_to_1_nats": ref.covariance_kl(covariances["exact"][0], covariances["exact"][1], noise_variance),
            "kl_1_to_0_nats": ref.covariance_kl(covariances["exact"][1], covariances["exact"][0], noise_variance),
            "bhattacharyya_nats": ref.covariance_bhattacharyya(covariances["exact"][0], covariances["exact"][1], noise_variance),
        }}
        for name in list(models)[1:]:
            distances[name] = {
                "kl_0_to_1_nats": ref.covariance_kl(covariances[name][0], covariances[name][1], noise_variance),
                "kl_1_to_0_nats": ref.covariance_kl(covariances[name][1], covariances[name][0], noise_variance),
                "bhattacharyya_nats": ref.covariance_bhattacharyya(covariances[name][0], covariances[name][1], noise_variance),
            }
        counts = {precision: {name: {"by_truth": []} for name in models}
                  for precision in ("fp64", "fp32-bulk")}
        means = {precision: {name: [] for name in models}
                 for precision in ("fp64", "fp32-bulk")}
        max_abs_score_delta = {name: 0.0 for name in models}
        fp32_winner_disagreements = {name: 0 for name in models}
        score_seconds = {precision: 0.0 for precision in ("fp64", "fp32-bulk")}
        for truth in (0, 1):
            trial_seed = int(rng.integers(0, 2**63, dtype=np.int64))
            observations = ref.proper_complex_gaussian_samples(
                factors["exact"][truth], args.trials, seed=trial_seed)
            paired_predictions = {precision: {} for precision in ("fp64", "fp32-bulk")}
            for precision in ("fp64", "fp32-bulk"):
                score_started = time.perf_counter()
                for name in models:
                    score = np.stack([
                        ref.gaussian_log_likelihood(observations, factors[name][bit], precision=precision)
                        for bit in (0, 1)], axis=1)
                    prediction = np.argmax(score, axis=1)
                    paired_predictions[precision][name] = prediction.copy()
                    errors = int(np.count_nonzero(prediction != truth))
                    counts[precision][name]["by_truth"].append({
                        "truth_bit": truth, "errors": errors, "trials": args.trials,
                        "error_rate": errors / args.trials,
                        "wilson_95": wilson(errors, args.trials),
                    })
                    signed_llr = score[:, 1] - score[:, 0]
                    expected_sign = 1.0 if truth == 1 else -1.0
                    means[precision][name].append({
                        "truth_bit": truth,
                        "mean_correct_minus_wrong_log_likelihood_nats": float(expected_sign * signed_llr.mean()),
                    })
                score_seconds[precision] += time.perf_counter() - score_started
                if precision == "fp32-bulk":
                    for name in models:
                        bulk_score = np.stack([
                            ref.gaussian_log_likelihood(observations, factors[name][bit], precision="fp32-bulk")
                            for bit in (0, 1)], axis=1)
                        reference_score = np.stack([
                            ref.gaussian_log_likelihood(observations, factors[name][bit], precision="fp64")
                            for bit in (0, 1)], axis=1)
                        max_abs_score_delta[name] = max(
                            max_abs_score_delta[name],
                            float(np.max(np.abs(bulk_score - reference_score))))
            for name in models:
                fp32_winner_disagreements[name] += int(np.count_nonzero(
                    paired_predictions["fp32-bulk"][name] != paired_predictions["fp64"][name]))
        for precision in counts:
            for name in models:
                rows = counts[precision][name]["by_truth"]
                errors = sum(r["errors"] for r in rows)
                total = sum(r["trials"] for r in rows)
                counts[precision][name]["total"] = {
                    "errors": errors, "trials": total, "error_rate": errors/total,
                    "wilson_95": wilson(errors, total),
                }
        results.append({
            "cn0_db_hz": cn0, "noise_variance": noise_variance,
            "covariance_distances_nats": distances,
            "classification": counts, "mean_scores": means,
            "fp32_bulk_max_abs_log_likelihood_delta_nats": max_abs_score_delta,
            "fp32_vs_fp64_winner_disagreements": {
                name: {"disagreements": disagreements, "trials": 2 * args.trials,
                       "rate": disagreements / (2 * args.trials)}
                for name, disagreements in fp32_winner_disagreements.items()
            },
            "classification_scoring_seconds": score_seconds,
            "known_realized_channel_awgn_classification_bound": {
                "assumption": "both candidate waveforms and the shared two-tap complex channel realization are known; only white proper-complex receiver noise is unknown",
                "mean_error_probability": float(known_channel_pe.mean()),
                "median_error_probability": float(np.median(known_channel_pe)),
                "p05_error_probability": float(np.quantile(known_channel_pe, 0.05)),
                "p95_error_probability": float(np.quantile(known_channel_pe, 0.95)),
                "realizations": args.trials,
            },
        })

    source_file = Path(ref.__file__).resolve()
    script_path = Path(__file__).resolve()
    receipt = {
        "title": "Matched proper-complex Gaussian covariance reference on retained public basis",
        "status": "diagnostic matched-prior screen; not end-to-end receiver performance",
        "scope_limits": [
            "No false admission or calibrated operational admission was tested; this is bit-vs-bit classification only.",
            "The finite Each-65 selective-modulation law was not modeled in the Gaussian likelihood; the matched waveform basis is reused but the channel prior is OFF-selective.",
            "No transmitter-duration extrapolation or universal optimality claim is made.",
            "Tap coefficients follow the declared zero-mean proper-complex squared-exponential prior; this is not a finite realized-channel ensemble.",
            "Known-realized-channel Q values are conditional equal-prior bit-classification bounds under a shared h draw; they are not signal-admission bounds or receiver results.",
            "Wilson intervals are pointwise diagnostics and are not simultaneous confidence coverage over all C/N0, model and precision comparisons.",
        ],
        "configuration": {
            "basis_candidates": [2, 3], "candidate_bits": [0, 1],
            "basis_window_samples_half_open": [start, stop], "window_samples": 256,
            "window_duration_seconds": 256/sample_rate, "sample_rate_hz": sample_rate,
            "window_first_timestamp_seconds": start/sample_rate,
            "window_last_timestamp_seconds": (stop-1)/sample_rate,
            "doppler_width_hz": 30.0, "tap_powers": [0.5, 0.5],
            "tap_column_mean_power_normalization": tap_norms.tolist(),
            "noise_variance_formula": "Fs / 10^(C/N0_dBHz/10)",
            "cn0_db_hz": cn0_values, "trials_per_truth_per_cn0": args.trials,
            "random_seed": args.seed, "block_models": {"block32": 32, "block16": 16, "block8": 8},
            "known_channel_seed": args.seed + 71,
            "known_channel_formula_check": {
                "distance": antipodal_distance, "noise_variance": antipodal_noise_variance,
                "reference_helper_probability": antipodal_bound,
                "ndtr_probability": antipodal_expected,
            },
            "known_channel_psd_roundoff_policy": "eigendecompose symmetrized SE covariance; clamp negative eigenmodes only if magnitude <=1e-12 times largest eigenvalue; no diagonal jitter",
            "block_window_samples": 256,
            "block_window_interpretation": "fixed 0.125-second physical window; covariance approximation uses block centers and declares independent 256-sample windows",
            "score_modes": ["fp64", "fp32-bulk"],
            "fp32_bulk_contract": "FP32 triangular whitening only; covariance construction, Cholesky, logdet, quadratic/evidence reductions FP64",
            "triangular_solve_implementation": "scipy.linalg.solve_triangular",
            "noise_draw_pairing": "same exact-observation draws reused across every approximate covariance and precision mode for each truth/CN0",
        },
        "inputs": {
            "manifest": str(manifest_path), "manifest_sha256": sha256(manifest_path),
            "basis": str(basis_path), "basis_sha256": sha256(basis_path),
            "basis_manifest_selective_perturbation": manifest.get("channel_model", {}).get("selective_model"),
            "basis_manifest_selective_depths": [manifest.get("channel_model", {}).get("perturbation_slow_depth"), manifest.get("channel_model", {}).get("perturbation_fast_depth")],
            "basis_manifest_ray_delay_doppler": [[tap["delay_seconds"], tap["doppler_hz"]] for tap in manifest["candidates"][2]["taps"]],
            "basis_source_metadata": "candidate waveform was exported with Each-65; basis use does not make the Gaussian coefficient prior an exact finite-modulation likelihood",
            "covariance_reference_source": str(source_file), "covariance_reference_sha256": sha256(source_file),
            "script": str(script_path), "script_sha256": sha256(script_path),
            "source_revision": os.popen("git rev-parse HEAD").read().strip(),
        },
        "runtime_seconds": time.perf_counter()-began,
        "environment": {"python": sys.version, "numpy": np.__version__,
                        "scipy": scipy.__version__,
                        "openblas_threads": os.environ.get("OPENBLAS_NUM_THREADS"),
                        "omp_threads": os.environ.get("OMP_NUM_THREADS")},
        "results": results,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(f"refusing to overwrite evidence: {output}")
    output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(output), "runtime_seconds": receipt["runtime_seconds"],
                      "basis_sha256": receipt["inputs"]["basis_sha256"],
                      "cn0_db_hz": cn0_values, "trials_per_truth": args.trials}, indent=2))


if __name__ == "__main__":
    main()
