#!/usr/bin/env python3
"""Deterministic sampling refinement of the actual triad streaming channel.

Exports encrypted sampled waveforms and public flat/coupled-ray templates from
triad_long. No oscillator presets, clock policy or stochastic path laws are
enabled. Equal random seeds at different sample rates do not couple stochastic
paths; this study makes no such claim. Results are numerical diagnostics, not
receiver sensitivity or a calibrated physical propagation model.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "triad_bounded_run.py"
PROFILES = ("baseline", "diurnal", "outage")
SEEDS = {"key": 20261101, "channel": 151, "path": 707}


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024**2), b""):
            h.update(chunk)
    return h.hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def run(command, directory, timeout, term_grace=2.):
    """Delegate group ownership/reaping to the shared bounded runner.

    Forward driver INT/TERM while an exporter is active and finish the runner's
    bounded cleanup before returning. No blocking wait exceeds .1 seconds.
    Detached sessions remain outside the shared runner's ownership contract.
    """
    directory.mkdir()
    start = time.monotonic()
    env = dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1", MKL_NUM_THREADS="1")
    receipt_path = directory / "runner-status.json"
    runner_command = [sys.executable, str(RUNNER), "--cwd", str(Path.cwd()),
                      "--log", str(directory / "export.log"), "--status", str(receipt_path),
                      "--timeout", str(timeout), "--max-seconds", str(timeout),
                      "--term-grace", str(term_grace), "--", *command]
    received_signal = None
    previous = {}
    child = None

    def cancel(signum, _frame):
        nonlocal received_signal
        if received_signal is None:
            received_signal = signum

    for sig in (signal.SIGINT, signal.SIGTERM):
        previous[sig] = signal.signal(sig, cancel)
    try:
        with (directory / "stdout.log").open("w") as out, (directory / "stderr.log").open("w") as err:
            child = subprocess.Popen(runner_command, stdin=subprocess.DEVNULL, stdout=out, stderr=err, env=env)
            forwarded = False
            progress_at = start + 30
            while child.poll() is None:
                if received_signal is not None and not forwarded:
                    try:
                        child.send_signal(received_signal)
                    except ProcessLookupError:
                        pass
                    forwarded = True
                try:
                    child.wait(timeout=.1)
                except subprocess.TimeoutExpired:
                    if time.monotonic() >= progress_at:
                        print(json.dumps({"running": directory.name, "seconds": time.monotonic()-start}), flush=True)
                        progress_at = time.monotonic() + 30
    finally:
        # Unexpected Python exceptions must also let the owning runner perform
        # TERM/KILL/reaping; killing the runner itself would strand its group.
        if child is not None and child.poll() is None:
            try:
                child.terminate()
            except ProcessLookupError:
                pass
            while child.poll() is None:
                try:
                    child.wait(timeout=.1)
                except subprocess.TimeoutExpired:
                    continue
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    result = {"status": "failed", "returncode": child.returncode,
              "elapsed_seconds": time.monotonic()-start, "command": command,
              "runner_command": runner_command}
    try:
        receipt = json.loads(receipt_path.read_text())
        result["runner_status"] = receipt
        if received_signal is not None:
            result.update(status="cancelled", returncode=128+received_signal)
        elif receipt["outcome"] == "timeout":
            result["status"] = "timeout"
        elif child.returncode == 0 and receipt["exit_code"] == 0 and receipt["termination"]["group_empty"]:
            result["status"] = "complete"
    except (OSError, ValueError, KeyError, TypeError) as error:
        result["runner_validation_error"] = str(error)
        if received_signal is not None:
            result.update(status="cancelled", returncode=128+received_signal)
    return result


def base_command(args, rate, fs):
    return [str(args.binary), "--mode", "export", "--seconds", "8", "--rate", str(rate),
            "--added", "1", "--sample-rate", str(fs), "--rf-hz", "8000",
            "--spread-preset", "sub9-best", "--margin", "1", "--selective-perturbation", "off",
            "--phase-diffusion", "0", "--basis", "two-ray", "--tap-delay-ms", ".005",
            "--tap-doppler-hz", ".001", "--threads", "1", "--chunk", "4096",
            "--memory-mib", "32", "--export-max-mib", "256", "--key-seed", str(SEEDS["key"]),
            "--channel-seed", str(SEEDS["channel"]), "--path-seed", str(SEEDS["path"])]


def inspect(directory):
    m = json.loads((directory / "manifest.json").read_text())
    if m["complex_type"] != "complex64" or m["layout"] != "candidate,sample,column":
        raise ValueError("unsupported export layout")
    if any(m["endpoint_truth"][key] != 0 for key in
           ("tx_audio_ppm", "rx_audio_ppm", "tx_rf_ppm", "rx_rf_ppm",
            "tx_phase_diffusion_rad_sqrt_s", "rx_phase_diffusion_rad_sqrt_s")):
        raise ValueError("deterministic refinement requires zero endpoint errors")
    resolved = m["resolved_configuration"]
    if resolved["clock_policy_active"] or resolved["oscillator_presets_active"]:
        raise ValueError("unexpected timing or oscillator draws")
    if m["truth_clock_ppm"] or m["truth_frequency_hz"] or m["truth_frequency_drift_hz_per_second"]:
        raise ValueError("nonzero unplanned truth parameters")
    fs = m["sample_rate"]
    # No clock policy and zero initial physical timing: truth_arrival is exactly
    # the public capture lead. Do not use this equality for arbitrary captures.
    lead = m["truth_arrival_seconds"]
    origin = m["source_format"]["calendar_origin_sample"] / fs
    signal = np.memmap(directory / m["signal_file"], dtype=np.complex64, mode="r", shape=tuple(m["signal_shape"]))
    basis = np.memmap(directory / m["basis_file"], dtype=np.complex64, mode="r", shape=tuple(m["basis_shape"]))
    return {"manifest": m, "fs": fs, "lead": lead, "origin": origin,
            "signal": signal, "basis": basis, "rows": len(signal)}


def whitener(gram):
    values, vectors = np.linalg.eigh((gram + gram.conj().T) * .5)
    threshold = max(float(values[-1]), 0.) * 1e-10
    keep = values > max(threshold, 1e-15)
    return vectors[:, keep] / np.sqrt(values[keep]), values


def metrics(data):
    m, basis, signal = data["manifest"], data["basis"], data["signal"]
    candidates, _, columns = basis.shape
    dimension = candidates * columns
    gram = np.zeros((dimension, dimension), dtype=np.complex128)
    matched = np.zeros(dimension, dtype=np.complex128)
    energy = 0.
    for start in range(0, data["rows"], 32768):
        end = min(data["rows"], start+32768)
        A = np.asarray(basis[:, start:end, :].transpose(1, 0, 2).reshape(end-start, dimension), dtype=np.complex128)
        y = np.asarray(signal[start:end], dtype=np.complex128)
        gram += A.conj().T @ A
        matched += A.conj().T @ y
        energy += float(np.vdot(y, y).real)
    gram /= data["fs"]
    matched /= data["fs"]
    energy /= data["fs"]
    fitted = []
    whitening = []
    for i, c in enumerate(m["candidateparams"]):
        sl = slice(i*columns, (i+1)*columns)
        W, eigenvalues = whitener(gram[sl, sl])
        whitening.append(W)
        projected = float(np.linalg.norm(W.conj().T @ matched[sl])**2)
        fitted.append({"candidate": i, "bit": c["bit"], "tap_count": c["tap_count"],
                       "rank": W.shape[1], "gram_eigenvalues_seconds": eigenvalues.tolist(),
                       "projected_energy_seconds": projected,
                       "retained_signal_energy_fraction": projected/energy if energy else None})
    separation = []
    for i, a in enumerate(m["candidateparams"]):
        for j, b in enumerate(m["candidateparams"][:i]):
            if a["bit"] == b["bit"] or a["tap_count"] != b["tap_count"]:
                continue
            si, sj = slice(i*columns, (i+1)*columns), slice(j*columns, (j+1)*columns)
            overlap = whitening[i].conj().T @ gram[si, sj] @ whitening[j]
            singular = np.linalg.svd(overlap, compute_uv=False)
            separation.append({"candidates": [j, i], "tap_count": a["tap_count"],
                               "principal_cosines": singular.tolist(),
                               "worst_projection_fraction": float(singular[0]**2) if singular.size else None})
    return {"samples": data["rows"], "sample_rate": data["fs"],
            "observation_seconds": data["rows"]/data["fs"],
            "requested_active_seconds": m["requested_active_seconds"],
            "rounded_active_seconds": m["actual_elapsed_seconds"],
            "calendar_origin_seconds": data["origin"], "capture_lead_seconds": data["lead"],
            "signal_energy_seconds": energy,
            "path_evolution": m["channel_model"]["path_evolution"],
            "candidate_projection": fitted, "pattern_subspace_separation": separation,
            "gram_real_seconds": gram.real.tolist(), "gram_imag_seconds": gram.imag.tolist()}, gram


def compare(data, reference, gram, reference_gram):
    fs, rf = data["fs"], reference["fs"]
    shift = reference["origin"] + reference["lead"] - data["origin"] - data["lead"]
    # Mapping n_ref=Fs_ref*(n/Fs + shift) aligns payload calendars, not sample0.
    # These prescribed integer sample rates and sample-quantized halo origins
    # give coincident sample times; reject accidental interpolation shortcuts.
    scale = rf/fs
    offset = rf*shift
    if abs(scale-round(scale)) > 1e-9 or abs(offset-round(offset)) > 1e-8:
        raise ValueError("requested calendars do not have coincident comparison samples")
    step, offset = round(scale), round(offset)
    lo = max(0, math.ceil(-offset/step))
    hi = min(data["rows"], (reference["rows"]-1-offset)//step+1)
    delta = data["origin"]-reference["origin"]
    difference = ref_energy = 0.
    source_difference = source_energy = 0.
    inner = 0j
    low_energy = 0.
    for start in range(lo, hi, 32768):
        end = min(hi, start+32768)
        indices = np.arange(start, end)*step+offset
        y = np.asarray(data["signal"][start:end], dtype=np.complex128)
        r = np.asarray(reference["signal"][indices], dtype=np.complex128)
        x = np.asarray(data["basis"][0, start:end, 0], dtype=np.complex128)
        xr = np.asarray(reference["basis"][0, indices, 0], dtype=np.complex128)
        difference += float(np.vdot(y-r, y-r).real)
        ref_energy += float(np.vdot(r, r).real)
        low_energy += float(np.vdot(y, y).real)
        inner += np.vdot(r, y)
        source_difference += float(np.vdot(x-xr, x-xr).real)
        source_energy += float(np.vdot(xr, xr).real)
    # Doppler templates evaluated at different padded payload origins differ
    # by known column phases. Compare the same Gram after this unitary change
    # of basis; it changes neither projections nor singular subspace angles.
    phases = []
    for c in data["manifest"]["candidateparams"]:
        for j in range(data["basis"].shape[2]):
            f = c["taps"][j]["doppler_hz"] if j < c["tap_count"] else 0.
            phases.append(np.exp(2j*np.pi*f*delta))
    phases = np.asarray(phases)
    corrected_gram = phases[:, None] * gram * phases.conj()[None, :]
    return {"reference_sample_rate": rf, "common_sample_rows": hi-lo,
            "payload_calendar_origin_difference_seconds": delta,
            "reference_index_scale": step, "reference_index_offset": offset,
            "raw_aligned_signal_relative_l2": math.sqrt(difference/ref_energy),
            "aligned_signal_squared_correlation": float(abs(inner)**2/(low_energy*ref_energy)),
            "aligned_flat_source_basis_relative_l2": math.sqrt(source_difference/source_energy),
            "two_ray_phase_origin_difference_radians": 2*math.pi*.001*delta,
            "raw_gram_relative_frobenius": float(np.linalg.norm(gram-reference_gram)/np.linalg.norm(reference_gram)),
            "doppler_origin_compensated_gram_relative_frobenius": float(np.linalg.norm(corrected_gram-reference_gram)/np.linalg.norm(reference_gram)),
            "raw_curve_scope": "includes changed deterministic ray/diurnal phase at payload origin; not solely discretization error"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, default=ROOT/"build/dev/triad_long")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--rates", type=float, nargs="+", default=[.36, .036])
    p.add_argument("--sample-rates", type=int, nargs="+", default=[1, 4, 64])
    p.add_argument("--profiles", choices=PROFILES, nargs="+", default=list(PROFILES))
    p.add_argument("--timeout", type=float, default=300, help="export runtime ceiling; shared runner adds bounded cleanup")
    p.add_argument("--term-grace", type=float, default=2, help="TERM grace before shared runner KILL/reap (0..30s)")
    p.add_argument("--keep-raw", action="store_true")
    args = p.parse_args()
    args.binary = args.binary.resolve(strict=True)
    if not math.isfinite(args.timeout) or not 1 <= args.timeout <= 1800:
        p.error("timeout must be in1..1800 seconds")
    if not math.isfinite(args.term_grace) or not 0 < args.term_grace <= 30:
        p.error("term grace must be positive and at most30 seconds")
    if not args.rates or any(not math.isfinite(r) or r <= 0 for r in args.rates):
        p.error("rates must be finite and positive")
    if any(fs not in (1, 4, 64) for fs in args.sample_rates) or 64 not in args.sample_rates:
        p.error("sample rates must be distinct members of1,4,64 and include64")
    if len(args.sample_rates) != len(set(args.sample_rates)):
        p.error("sample rates must be distinct")
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "triad-deterministic-sampling-refinement-v1", "binary": str(args.binary),
              "binary_sha256": sha256(args.binary), "script_sha256": sha256(__file__),
              "runner_sha256": sha256(RUNNER),
              "seeds": SEEDS, "numpy_version": np.__version__, "cases": [], "comparisons": [],
              "limits": ["Deterministic waveform/channel only: no stochastic cross-rate path equivalence.",
                         "Fs64 is a refinement reference, not an analog or field-measurement certificate.",
                         "Complex64 exports; FP64 Gram and energy reductions with numerical rank cutoff1e-10.",
                         "Projection uses the actual summed waveform and full cross-column Gram; no independent-carrier assumption.",
                         "No noise, detection probability, false-admission calibration or sensitivity conclusion.",
                         "Raw aligned signal differences include payload-origin-dependent channel phase."]}
    for rate in args.rates:
        baseline_manifests = {}
        for profile in args.profiles:
            loaded, grams, entries = {}, {}, {}
            for fs in sorted(args.sample_rates):
                if fs <= 4.125*rate/6:
                    report["cases"].append({"rate": rate, "sample_rate": fs, "status": "unsupported_shaped_bandwidth"})
                    continue
                label = f"rate-{rate:g}-{profile}-fs-{fs}"
                directory = args.output/label
                command = base_command(args, rate, fs)
                if profile == "diurnal":
                    command += ["--diurnal-phase-radians", ".5"]
                if profile == "outage":
                    # Obtain geometry from the already-completed baseline export
                    # at this rate. Mid-payload coordinates ensure nonzero signal
                    # overlap, unlike a fixed100..500s interval at very low rates.
                    if fs not in baseline_manifests:
                        raise ValueError("outage requires baseline earlier in --profiles")
                    b = baseline_manifests[fs]
                    center = b["source_format"]["calendar_origin_sample"]/fs + b["actual_elapsed_seconds"]*.5
                    half = min(200., b["actual_elapsed_seconds"]*.15)
                    ramp = min(30., half*.25)
                    command += ["--outage-seconds", str(center-half), str(center+half),
                                "--outage-gain", "0", "--outage-ramp-seconds", str(ramp)]
                command += ["--output", str(directory)]
                print(json.dumps({"start": label}), flush=True)
                entry = {"rate": rate, "profile": profile, "sample_rate": fs}
                entry.update(run(command, directory, args.timeout, args.term_grace))
                report["cases"].append(entry)
                if entry["status"] == "cancelled":
                    report["cancelled"] = True
                    save(args.output/"results.json", report)
                    return entry["returncode"]
                if entry["status"] == "complete":
                    data = inspect(directory)
                    entry["metrics"], gram = metrics(data)
                    entry["file_sha256"] = {n: sha256(directory/n) for n in ("manifest.json", "basis.c64", "signal.c64")}
                    if profile == "baseline":
                        baseline_manifests[fs] = data["manifest"]
                    loaded[fs], grams[fs], entries[fs] = data, gram, entry
                save(args.output/"results.json", report)
                print(json.dumps({"finished": label, "status": entry["status"], "seconds": entry["elapsed_seconds"]}), flush=True)
            if 64 in loaded:
                for fs, data in loaded.items():
                    if fs != 64:
                        result = {"rate": rate, "profile": profile, "sample_rate": fs}
                        result.update(compare(data, loaded[64], grams[fs], grams[64]))
                        result["signal_energy_ratio_to_reference"] = entries[fs]["metrics"]["signal_energy_seconds"]/entries[64]["metrics"]["signal_energy_seconds"]
                        report["comparisons"].append(result)
            for fs, data in loaded.items():
                # Close mapped files before optional unlink; retain commands,
                # manifests, logs and hashes as compact reproducibility evidence.
                data["signal"]._mmap.close()
                data["basis"]._mmap.close()
                if not args.keep_raw:
                    directory = args.output/f"rate-{rate:g}-{profile}-fs-{fs}"
                    for name in ("basis.c64", "signal.c64"):
                        (directory/name).unlink()
            save(args.output/"results.json", report)
    report["completed_exports"] = sum(c.get("status") == "complete" for c in report["cases"])
    report["quality_pass_claimed"] = False
    save(args.output/"results.json", report)
    print(json.dumps({"report": str(args.output/"results.json"), "completed_exports": report["completed_exports"]}), flush=True)
    return int(any(c["status"] != "complete" for c in report["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
