#!/usr/bin/env python3
"""Frozen Gaussian bit-classification confirmation; NOT acquisition/admission.

Reuse run_reference.py's 256-sample public basis window and OFF-selective,
zero-mean proper Gaussian channel law. Exact at 25.25 dB-Hz versus block16 at
26.25 dB-Hz; full confirmation uses 1,048,576 samples PER TRUTH PER POINT.
Default is a 256-sample smoke only. No key, timing, epoch or duration search,
finite Each-65 law, false admission, continuous operation or long-span claim.

Example (repository root; choose the desired torch installation):
  OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 <this-script> \
    --device cpu --precision fp32 --output /tmp/gaussian-smoke.json
For the frozen confirmation explicitly pass --sampling-law quadratic
--precision fp64 --trials-per-bit 1048576.
CPU FP64 RNG supplies identical standard draws across precision/device/batch;
ALL bulk generation and whitening GEMMs use the selected complex precision.
CPU factors/logdets and device quadratic evidence reductions remain FP64.
The --sampling-law quadratic --precision fp64 mode samples the exact
marginal classifier law: LLR=c+sum(lambda_j*Exp_j(1)). CPU FP64 diagonalization
costs O(N^3) per point/truth, then O(N*trials), versus raw O(N^2*trials).
It generates no observations and is not paired with raw CPU/GPU samples.
Quadratic FP64 is the sole confirmatory sampler; raw is an arithmetic/throughput
diagnostic, avoiding selection between independent confidence-bound families.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
from pathlib import Path
import sys
import time

import numpy as np
import scipy
from scipy.linalg import solve_triangular
from scipy.stats import beta
import torch

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT))
from tools import triad_covariance_reference as ref

FULL = 1048576
ALPHA = .0125
FROZEN_BASIS = "376f2fe4cfc30b7da313a88bf80ab7cc291b4f774c42cc713c54441a1401f09a"
FROZEN_HELPER = "e6ca43a136d9781b6c0d9883e72d2069302bcf5ef285e7f8c70137b9fe9e31cb"


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cp_bound(errors, trials, direction):
    if direction == "lower":
        return 0. if errors == 0 else float(beta.ppf(ALPHA, errors, trials-errors+1))
    return 1. if errors == trials else float(beta.ppf(1-ALPHA, errors+1, trials-errors))


def standard_draws(rng, count):
    # Interleaved components preserve the RNG stream when batching changes.
    normal = rng.standard_normal((count, 256, 2))
    return (normal[..., 0] + 1j*normal[..., 1]) / np.sqrt(2.)


def run(args, report):
    device = torch.device(args.device)
    if device.type not in ("cpu", "cuda"):
        raise ValueError("only CPU/CUDA are qualified by this driver")
    torch.set_num_threads(args.threads)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    dtype = torch.complex128 if args.precision == "fp64" else torch.complex64
    timings = report["elapsed_seconds_by_phase"] = {}

    def timed(name, function):
        if device.type == "cuda":
            torch.cuda.synchronize(device)
        start = time.perf_counter()
        result = function()
        if device.type == "cuda":
            torch.cuda.synchronize(device)
        timings[name] = timings.get(name, 0.) + time.perf_counter()-start
        return result

    def tensor(array, arithmetic=dtype):
        return torch.as_tensor(array, dtype=arithmetic, device=device)

    def score(y, whiteners, logdet, measured=False):
        measure = timed if measured else lambda name, fn: fn()
        white = measure("whitening_gemm", lambda: y @ whiteners.transpose(-2, -1))
        quadratic = measure("fp64_evidence", lambda: (
            white.real.double().square() + white.imag.double().square()).sum(-1))
        return -quadratic.T-logdet

    report["software_hardware"] = {
        "python": platform.python_version(), "numpy": np.__version__,
        "scipy": scipy.__version__, "torch": torch.__version__,
        "cuda_runtime": torch.version.cuda, "device": str(device),
        "hardware": torch.cuda.get_device_name(device) if device.type == "cuda" else platform.machine(),
        "torch_threads": torch.get_num_threads(), "precision": args.precision,
        "generation_and_whitening_dtype": str(dtype) if args.sampling_law == "raw" else "preflight only; no raw Monte Carlo",
        "quadratic_montecarlo_dtype": "float64" if args.sampling_law == "quadratic" else None, "allow_tf32": False,
        "float32_matmul_precision": torch.get_float32_matmul_precision(),
        "preparation_logdet_and_evidence": "FP64; measured separately"}
    started = time.perf_counter()
    directory = args.basis_dir.resolve()
    manifest = json.loads((directory/"manifest.json").read_text())
    report["sha256"] = {"basis": sha256(directory/"basis.c128"),
        "manifest": sha256(directory/"manifest.json"), "helper": sha256(ref.__file__),
        "driver": sha256(__file__)}
    if manifest["basis_shape"] != [4, 19199, 2] or manifest["sample_rate"] != 2048:
        raise ValueError("fixture shape/rate differs from frozen reference")
    if report["sha256"]["basis"] != FROZEN_BASIS or report["sha256"]["helper"] != FROZEN_HELPER:
        raise ValueError("basis/helper hash differs from accepted frozen reference")
    basis = np.fromfile(directory/"basis.c128", dtype="<c16").reshape(4, 19199, 2)[[2, 3], 1024:1280].copy()
    powers = np.mean(abs(basis)**2, axis=1)
    if not np.isfinite(powers).all() or np.any(powers <= 0):
        raise ValueError("invalid normalization powers")
    basis /= np.sqrt(powers[:, None, :])
    times = np.arange(1024, 1280, dtype=np.float64)/2048
    exact = ref.channel_covariances(basis, times, 30., (.5, .5))
    block = ref.channel_covariances(basis, times, 30., (.5, .5), block_samples=16, window_samples=256)
    points = []
    for name, cn0, covariance, seed in (("exact", 25.25, exact, args.seed),
                                      ("block16", 26.25, block, args.seed+1)):
        noise = 2048/10**(cn0/10)
        truth = [ref.factor_covariance(k, noise) for k in exact]
        factors = truth if name == "exact" else [ref.factor_covariance(k, noise) for k in covariance]
        whiteners = np.stack([solve_triangular(f.cholesky, np.eye(256), lower=True, check_finite=False) for f in factors])
        points.append((name, cn0, seed, truth, factors, whiteners))
    timings["cpu_fp64_preparation"] = time.perf_counter()-started
    report["sample_law"] = {
        "distribution": "proper-complex CN(0,K_exact_truth + Fs/10^(CN0/10) I)",
        "coefficient_covariance": "exp(-0.5*(pi*30*(t_i-t_j))^2)",
        "window": [1024, 1280], "sample_rate": 2048, "sample_slot_seconds": .125,
        "candidate_rows": [2, 3], "column_mean_powers_before_normalization": powers.tolist(),
        "tap_powers": [.5, .5], "selective": "Off; basis export channel truth is not used",
        "observation_covariance_rank": 256, "signal_rank_tolerance_relative": 1e-10,
        "signal_ranks": {name: [int(np.count_nonzero((v := np.linalg.eigvalsh(k)) > 1e-10*v[-1])) for k in cov]
                         for name, cov in (("exact", exact), ("block16", block))},
        "rng": "NumPy PCG64; SeedSequence([point_seed,truth_bit]); interleaved real/imag N(0,1)/sqrt(2)",
        "replay": "standard draws paired across precision/device/batch for identical NumPy version; arithmetic differs",
        "matrix_convention": "Y=Z@L.T; whitened=Y@(L^-1).T; no conjugation in either row transform"}
    # Separate validation namespace; compare BOTH arithmetic modes to the accepted helper.
    started = time.perf_counter()
    checks = []
    for name, cn0, seed, truth, factors, whiteners in points:
        for bit in (0, 1):
            z = standard_draws(np.random.default_rng([2026110220, bit]), 16)
            y = z @ truth[bit].cholesky.T
            reference = np.stack([ref.gaussian_log_likelihood(y, f) for f in factors], axis=1)
            for label, arithmetic, tolerance in (("fp64", torch.complex128, 1e-8), ("fp32", torch.complex64, .003)):
                generated = (tensor(z, arithmetic) @ tensor(truth[bit].cholesky, arithmetic).T).cpu().numpy()
                observed = score(tensor(y, arithmetic), tensor(whiteners, arithmetic), tensor(np.array([f.log_determinant for f in factors]), torch.float64)).cpu().numpy()
                delta = float(np.max(abs(reference-observed)))
                generation_error = float(np.linalg.norm(generated-y)/np.linalg.norm(y))
                ranking_errors = int(np.count_nonzero(reference.argmax(1) != observed.argmax(1)))
                check = {"point": name, "truth": bit, "precision": label, "samples": 16,
                         "max_ll_absolute_error": delta, "generation_relative_l2_error": generation_error,
                         "ranking_disagreements": ranking_errors}
                checks.append(check)
                if not np.isfinite(delta+generation_error) or delta > tolerance or ranking_errors or generation_error > (1e-12 if label == "fp64" else 1e-6):
                    raise ValueError(f"arithmetic preflight failed: {check}")
    report["arithmetic_preflight"] = checks
    timings["arithmetic_preflight"] = time.perf_counter()-started
    quadratic_laws = {}
    if args.sampling_law == "quadratic":
        started = time.perf_counter()
        report["quadratic_preflight"] = []
        report["sample_law"].update({
            "sampling": "exact marginal classifier law; no raw observations generated in Monte Carlo",
            "rng": "NumPy PCG64 SeedSequence([point_seed,truth_bit,1]); iid standard_exponential",
            "replay": "quadratic batches share draws; raw and quadratic namespaces/draws are different",
            "llr_law": "LL1-LL0=logdet(R0)-logdet(R1)+sum_j lambda_j Exp_j(1)",
            "quadratic_arithmetic": "FP64 throughout; all 256 signed eigenvalues retained"})
        for name, cn0, seed, truth, factors, whiteners in points:
            constant = factors[0].log_determinant-factors[1].log_determinant
            for bit in (0, 1):
                l = truth[bit].cholesky
                # H=L^H(W0^H W0-W1^H W1)L, without forming covariance inverses.
                a0, a1 = whiteners[0] @ l, whiteners[1] @ l
                h = a0.conj().T @ a0-a1.conj().T @ a1
                eigenvalues, rotation = np.linalg.eigh((h+h.conj().T)/2)
                z = standard_draws(np.random.default_rng([2026110220, bit, 1]), 16)
                y = z @ rotation.T @ l.T
                reference = ref.gaussian_log_likelihood(y, factors[1])-ref.gaussian_log_likelihood(y, factors[0])
                reduced = constant+(z.real*z.real+z.imag*z.imag) @ eigenvalues
                delta = float(np.max(abs(reference-reduced)))
                disagreements = int(np.count_nonzero((reference > 0) != (reduced > 0)))
                report["quadratic_preflight"].append({"point": name, "truth": bit, "samples": 16,
                    "max_llr_absolute_error": delta, "ranking_disagreements": disagreements,
                    "eigenvalue_range": [float(eigenvalues[0]), float(eigenvalues[-1])]})
                if not np.isfinite(delta) or delta > 1e-10 or disagreements:
                    raise ValueError("quadratic exact-marginal law failed paired rotated-raw preflight")
                quadratic_laws[name, bit] = (constant, tensor(eigenvalues, torch.float64))
        timings["cpu_fp64_quadratic_preparation_and_preflight"] = time.perf_counter()-started
    report["points"] = rows = []
    for name, cn0, seed, truth, factors, whiteners in points:
        w = timed("factor_transfer", lambda: tensor(whiteners))
        logdet = tensor(np.array([f.log_determinant for f in factors]), torch.float64)
        for bit in (0, 1):
            l = timed("factor_transfer", lambda: tensor(truth[bit].cholesky))
            rng = np.random.default_rng([seed, bit] + ([1] if args.sampling_law == "quadratic" else []))
            errors = completed = 0
            began = time.perf_counter()
            row = {"model": name, "cn0_db_hz": cn0, "seed_namespace": seed, "truth": bit,
                   "trials": 0, "correct": 0, "errors": 0, "complete": False}
            rows.append(row)
            while completed < args.trials_per_bit:
                count = min(args.batch, args.trials_per_bit-completed)
                if args.sampling_law == "raw":
                    z = timed("cpu_rng_and_sample_transfer", lambda: tensor(standard_draws(rng, count)))
                    y = timed("generation_gemm", lambda: z @ l.T)
                    scores = score(y, w, logdet, measured=True)
                else:
                    constant, eigenvalues = quadratic_laws[name, bit]
                    exponentials = timed("cpu_exponential_rng_and_transfer", lambda: tensor(rng.standard_exponential((count, 256)), torch.float64))
                    llr = timed("fp64_quadratic_law", lambda: constant+exponentials @ eigenvalues)
                    scores = torch.stack((torch.zeros_like(llr), llr), dim=1)
                def collect():
                    if not bool(torch.isfinite(scores).all().item()):
                        raise ValueError("nonfinite Monte Carlo likelihood")
                    return int((scores.argmax(1) != bit).sum().item())
                errors += timed("decision_and_host_counts", collect)
                completed += count
                row.update(trials=completed, correct=completed-errors, errors=errors)
            direction = "lower" if name == "exact" else "upper"
            row.update({"complete": True, "ber": errors/completed, "cp_direction": direction,
                   "cp_bound": cp_bound(errors, completed, direction), "cp_one_sided_alpha": ALPHA,
                   "elapsed_seconds": time.perf_counter()-began})
            print(json.dumps(row), flush=True)
    lower = sum(r["cp_bound"] for r in rows[:2])/2
    upper = sum(r["cp_bound"] for r in rows[2:])/2
    frozen = args.trials_per_bit == FULL and args.seed == 2026110221
    confirmatory = frozen and args.sampling_law == "quadratic" and args.precision == "fp64"
    report["inference"] = {"family_alpha": .05, "four_one_sided_bounds_alpha_each": ALPHA,
        "reference_mean_ber_lower": lower, "practical_mean_ber_upper": upper,
        "frozen_count_and_seed": frozen, "confirmatory_sampler": "quadratic FP64 only; raw is diagnostic",
        "one_db_99_percent_classification_demonstrated": confirmatory and lower > .01 and upper < .01,
        "conclusion": "confirmed" if confirmatory and lower > .01 and upper < .01 else "inconclusive; no retuning",
        "scope": "equal bit priors, fixed 256-sample window, known synchronization/key and stated Gaussian law only; no admission results"}
    report["status"] = "complete"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basis-dir", type=Path, default=ROOT/"local-evidence/triad-acquisition-improvement/reference/public-basis")
    parser.add_argument("--trials-per-bit", type=int, default=256)
    parser.add_argument("--batch", type=int, default=4096)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--precision", choices=("fp64", "fp32"), default="fp32")
    parser.add_argument("--sampling-law", choices=("raw", "quadratic"), default="raw")
    parser.add_argument("--seed", type=int, default=2026110221)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.sampling_law == "quadratic" and args.precision != "fp64":
        parser.error("quadratic sampling currently requires --precision fp64")
    if not 1 <= args.trials_per_bit <= FULL or not 1 <= args.batch <= 4096 or args.seed < 0 or args.threads < 1:
        parser.error("require 1<=trials-per-bit<=1048576, 1<=batch<=4096, nonnegative seed, positive threads")
    report = {"status": "failed", "failures": [], "arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}}
    began = time.perf_counter()
    try:
        with torch.inference_mode():
            run(args, report)
    except Exception as error:
        report["failures"].append(f"{type(error).__name__}: {error}")
        raise
    finally:
        report["total_elapsed_seconds"] = time.perf_counter()-began
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")


if __name__ == "__main__":
    main()
