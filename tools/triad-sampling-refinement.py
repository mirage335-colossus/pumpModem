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
from fractions import Fraction
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


def sampling_grid(sample_rates, sample_periods):
    if any(not 1 <= value <= 2**32-1 for value in [*sample_rates, *sample_periods]):
        raise ValueError("sample rates and periods must be positive uint32 integers")
    grid = [Fraction(fs) for fs in sample_rates] + [Fraction(1, period) for period in sample_periods]
    if not grid or len(grid) != len(set(grid)):
        raise ValueError("sample rates/periods must be nonempty, distinct and nonoverlapping")
    return sorted(grid)


def sampling_fields(fs):
    fs = Fraction(fs)
    return {"sample_rate": fs.numerator if fs.denominator == 1 else float(fs),
            "sample_rate_numerator": fs.numerator, "sample_rate_denominator": fs.denominator,
            "sample_period_seconds": float(1/fs)}


def sampling_label(fs):
    fs = Fraction(fs)
    return str(fs.numerator) if fs.denominator == 1 else f"{fs.numerator}over{fs.denominator}"


def base_command(args, rate, fs):
    fs = Fraction(fs)
    command = [str(args.binary), "--mode", "export", "--seconds", "8", "--rate", str(rate),
            "--added", "1", "--sample-rate", str(fs.numerator), "--rf-hz", "8000",
            "--spread-preset", "sub9-best", "--margin", "1", "--selective-perturbation", "off",
            "--phase-diffusion", "0", "--basis", "two-ray", "--tap-delay-ms", ".005",
            "--tap-doppler-hz", ".001", "--threads", "1", "--chunk", "4096",
            "--memory-mib", "32", "--export-max-mib", "256", "--key-seed", str(SEEDS["key"]),
            "--channel-seed", str(SEEDS["channel"]), "--path-seed", str(SEEDS["path"])]
    if fs.denominator > 1:
        command += ["--sample-rate-denominator", str(fs.denominator)]
    return command


def capture_eligibility(rate, fs, profile):
    """Declared no-offset sub9-best case only; runtime remains authoritative."""
    bandwidth = 3*1.25*rate/6
    if float(fs) <= 1.1*bandwidth:
        return {"status": "unsupported_shaped_bandwidth", "required_sample_rate_exclusive": 1.1*bandwidth}
    diurnal = .5/86400 if profile == "diurnal" else 0.
    required = bandwidth + 2*(.001 + diurnal)
    if float(fs) <= required:
        return {"status": "unsupported_deterministic_capture", "required_sample_rate_exclusive": required,
                "reason": "triplet bandwidth plus twice the selected ray Doppler and diurnal excursion"}
    return None


def outage_profile(args, manifest, fs):
    elapsed = manifest["actual_elapsed_seconds"]
    requested_span = args.outage_span_seconds if args.outage_span_seconds is not None else min(400., elapsed*.3)
    requested_ramp = args.outage_ramp_seconds if args.outage_ramp_seconds is not None else min(30., requested_span/8)
    # One physical profile for the complete selected grid: no shorter ramps
    # secretly used for its faster members. Keep a nonzero plateau as well.
    ramp = max(requested_ramp, 4/float(min(args.sampling_grid)))
    span = max(requested_span, 3*ramp)
    center = manifest["source_format"]["calendar_origin_sample"]/float(fs) + elapsed*.5
    return {"start_seconds": center-span/2, "end_seconds": center+span/2,
            "span_seconds": span, "ramp_seconds": ramp,
            "requested_span_seconds": requested_span, "requested_ramp_seconds": requested_ramp,
            "slowest_selected_sample_rate_hz": float(min(args.sampling_grid)),
            "ramp_samples_at_slowest_rate": ramp*float(min(args.sampling_grid)),
            "adjusted_for_sampling": span != requested_span or ramp != requested_ramp,
            "clipped_to_tile": False,
            "coordinate": "mid-payload calendar plus this source's padding origin; common span/ramp across Fs"}


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
    numerator = m.get("sample_rate_numerator", m["sample_rate"])
    denominator = m.get("sample_rate_denominator", 1)
    if (not isinstance(numerator, int) or isinstance(numerator, bool) or
            not isinstance(denominator, int) or isinstance(denominator, bool) or
            numerator <= 0 or denominator <= 0 or (denominator > 1 and numerator != 1)):
        raise ValueError("invalid exported rational sample clock")
    rational_fs = Fraction(numerator, denominator)
    fs = float(rational_fs)
    if not math.isclose(fs, m["sample_rate"], rel_tol=1e-12, abs_tol=0):
        raise ValueError("effective sample rate disagrees with exported rational clock")
    # No clock policy and zero initial physical timing: truth_arrival is exactly
    # the public capture lead. Do not use this equality for arbitrary captures.
    lead = m["truth_arrival_seconds"]
    origin = m["source_format"]["calendar_origin_sample"] / fs
    signal = np.memmap(directory / m["signal_file"], dtype=np.complex64, mode="r", shape=tuple(m["signal_shape"]))
    basis = np.memmap(directory / m["basis_file"], dtype=np.complex64, mode="r", shape=tuple(m["basis_shape"]))
    return {"manifest": m, "fs": fs, "rational_fs": rational_fs, "lead": lead, "origin": origin,
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
    return {"samples": data["rows"], **sampling_fields(data["rational_fs"]),
            "observation_seconds": data["rows"]/data["fs"],
            "requested_active_seconds": m["requested_active_seconds"],
            "rounded_active_seconds": m["actual_elapsed_seconds"],
            "calendar_origin_seconds": data["origin"], "capture_lead_seconds": data["lead"],
            "signal_energy_seconds": energy,
            "path_evolution": m["channel_model"]["path_evolution"],
            "candidate_projection": fitted, "pattern_subspace_separation": separation,
            "gram_real_seconds": gram.real.tolist(), "gram_imag_seconds": gram.imag.tolist()}, gram


def aligned_sample_grid(data, reference):
    fs, rf = data["rational_fs"], reference["rational_fs"]
    origin = Fraction(data["manifest"]["source_format"]["calendar_origin_sample"])/fs
    ref_origin = Fraction(reference["manifest"]["source_format"]["calendar_origin_sample"])/rf
    shift = ref_origin + Fraction(str(reference["lead"])) - origin - Fraction(str(data["lead"]))
    # Exact rational sample lattice, including nonnested periods of3 and4 seconds.
    # Retain only coincident physical payload times; never interpolate curves.
    scale, offset = rf/fs, rf*shift
    divisor = math.lcm(scale.denominator, offset.denominator)
    a, b = int(scale*divisor), int(offset*divisor)
    common = math.gcd(a, divisor)
    if b % common:
        raise ValueError("requested calendars have no coincident comparison samples")
    stride = divisor//common
    residue = 0 if stride == 1 else (-b//common)*pow(a//common, -1, stride) % stride
    lower = max(0, math.ceil(-offset/scale))
    upper = min(data["rows"]-1, math.floor((reference["rows"]-1-offset)/scale))
    first = lower + (residue-lower) % stride
    count = max(0, (upper-first)//stride+1)
    if not count:
        raise ValueError("aligned captures have no common sample extent")
    return first, stride, int(first*scale+offset), int(stride*scale), count, scale, offset


def compare(data, reference, gram, reference_gram):
    first, stride, ref_first, ref_stride, count, scale, offset = aligned_sample_grid(data, reference)
    delta = data["origin"]-reference["origin"]
    difference = ref_energy = 0.
    source_difference = source_energy = 0.
    inner = 0j
    low_energy = 0.
    for start in range(0, count, 32768):
        row = np.arange(start, min(count, start+32768))
        indices, ref_indices = first+row*stride, ref_first+row*ref_stride
        y = np.asarray(data["signal"][indices], dtype=np.complex128)
        r = np.asarray(reference["signal"][ref_indices], dtype=np.complex128)
        x = np.asarray(data["basis"][0, indices, 0], dtype=np.complex128)
        xr = np.asarray(reference["basis"][0, ref_indices, 0], dtype=np.complex128)
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
    return {"reference_sample_rate": reference["fs"], "common_sample_rows": count,
            "payload_calendar_origin_difference_seconds": delta,
            "reference_index_scale": float(scale), "reference_index_offset": float(offset),
            "compared_sample_stride": stride, "reference_compared_sample_stride": ref_stride,
            "raw_aligned_signal_relative_l2": math.sqrt(difference/ref_energy),
            "aligned_signal_squared_correlation": float(abs(inner)**2/(low_energy*ref_energy)),
            "aligned_flat_source_basis_relative_l2": math.sqrt(source_difference/source_energy),
            "two_ray_phase_origin_difference_radians": 2*math.pi*.001*delta,
            "raw_gram_relative_frobenius": float(np.linalg.norm(gram-reference_gram)/np.linalg.norm(reference_gram)),
            "doppler_origin_compensated_gram_relative_frobenius": float(np.linalg.norm(corrected_gram-reference_gram)/np.linalg.norm(reference_gram)),
            "raw_curve_scope": "includes changed deterministic ray/diurnal phase at payload origin; not solely discretization error"}


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, default=ROOT/"build/dev/triad_long")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--rates", type=float, nargs="+", default=[.36, .036])
    p.add_argument("--sample-rates", type=int, nargs="*", default=[1, 4, 64], help="integer Hz; empty list selects only --sample-periods")
    p.add_argument("--sample-periods", type=int, nargs="*", default=[], help="integer seconds/sample; combine with --sample-rates without overlaps")
    p.add_argument("--profiles", choices=PROFILES, nargs="+", default=list(PROFILES))
    p.add_argument("--timeout", type=float, default=300, help="export runtime ceiling; shared runner adds bounded cleanup")
    p.add_argument("--term-grace", type=float, default=2, help="TERM grace before shared runner KILL/reap (0..30s)")
    p.add_argument("--outage-span-seconds", type=float, help="requested common span; default min(400s,0.3*tile); expanded to at least3 ramps")
    p.add_argument("--outage-ramp-seconds", type=float, help="requested common ramp; default min(30s,span/8); floor4 slowest-grid samples")
    p.add_argument("--keep-raw", action="store_true")
    args = p.parse_args(argv)
    args.binary = args.binary.resolve(strict=True)
    if not math.isfinite(args.timeout) or not 1 <= args.timeout <= 1800:
        p.error("timeout must be in1..1800 seconds")
    if not math.isfinite(args.term_grace) or not 0 < args.term_grace <= 30:
        p.error("term grace must be positive and at most30 seconds")
    if not args.rates or any(not math.isfinite(r) or r <= 0 for r in args.rates):
        p.error("rates must be finite and positive")
    try:
        args.sampling_grid = sampling_grid(args.sample_rates, args.sample_periods)
    except ValueError as error:
        p.error(str(error))
    if len(set(args.rates)) != len(args.rates) or len(set(args.profiles)) != len(args.profiles):
        p.error("rates and profiles must be distinct")
    if "outage" in args.profiles and ("baseline" not in args.profiles or args.profiles.index("baseline") > args.profiles.index("outage")):
        p.error("outage requires baseline earlier in --profiles")
    for name in ("outage_span_seconds", "outage_ramp_seconds"):
        value = getattr(args, name)
        if value is not None and (not math.isfinite(value) or value <= 0):
            p.error(name + " must be positive finite")
    return args


def main():
    args = parse_args()
    reference_fs = max(args.sampling_grid)
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "triad-deterministic-sampling-refinement-v1", "binary": str(args.binary),
              "binary_sha256": sha256(args.binary), "script_sha256": sha256(__file__),
              "runner_sha256": sha256(RUNNER),
              "sampling_grid": [sampling_fields(fs) for fs in args.sampling_grid],
              "reference_sample_rate": float(reference_fs),
              "seeds": SEEDS, "numpy_version": np.__version__, "cases": [], "comparisons": [],
              "limits": ["Deterministic waveform/channel only: no stochastic cross-rate path equivalence.",
                         "The highest selected Fs is a refinement reference, not an analog or field-measurement certificate.",
                         "Complex64 exports; FP64 Gram and energy reductions with numerical rank cutoff1e-10.",
                         "Projection uses the actual summed waveform and full cross-column Gram; no independent-carrier assumption.",
                         "No noise, detection probability, false-admission calibration or sensitivity conclusion.",
                         "Raw aligned signal differences include payload-origin-dependent channel phase."]}
    for rate in args.rates:
        baseline_manifests = {}
        for profile in args.profiles:
            loaded, grams, entries = {}, {}, {}
            for fs in args.sampling_grid:
                entry = {"rate": rate, "profile": profile, **sampling_fields(fs)}
                unsupported = capture_eligibility(rate, fs, profile)
                if unsupported:
                    report["cases"].append({**entry, **unsupported})
                    continue
                label = f"rate-{rate:g}-{profile}-fs-{sampling_label(fs)}"
                directory = args.output/label
                command = base_command(args, rate, fs)
                if profile == "diurnal":
                    command += ["--diurnal-phase-radians", ".5"]
                if profile == "outage":
                    # Obtain geometry from the already-completed baseline export
                    # at this rate. Mid-payload coordinates ensure nonzero signal
                    # overlap, unlike a fixed100..500s interval at very low rates.
                    if fs not in baseline_manifests:
                        report["cases"].append({**entry, "status": "baseline_unavailable"})
                        continue
                    b = baseline_manifests[fs]
                    outage = outage_profile(args, b, fs)
                    entry["outage_sampling_profile"] = outage
                    if outage["start_seconds"] < 0:
                        report["cases"].append({**entry, "status": "unsupported_outage_extent"})
                        continue
                    command += ["--outage-seconds", str(outage["start_seconds"]), str(outage["end_seconds"]),
                                "--outage-gain", "0", "--outage-ramp-seconds", str(outage["ramp_seconds"])]
                command += ["--output", str(directory)]
                print(json.dumps({"start": label}), flush=True)
                entry.update(run(command, directory, args.timeout, args.term_grace))
                report["cases"].append(entry)
                if entry["status"] == "cancelled":
                    report["cancelled"] = True
                    save(args.output/"results.json", report)
                    return entry["returncode"]
                if entry["status"] == "complete":
                    data = inspect(directory)
                    if data["rational_fs"] != fs:
                        raise ValueError("exporter sample clock differs from requested rate")
                    entry["metrics"], gram = metrics(data)
                    entry["file_sha256"] = {n: sha256(directory/n) for n in ("manifest.json", "basis.c64", "signal.c64")}
                    if profile == "baseline":
                        baseline_manifests[fs] = data["manifest"]
                    loaded[fs], grams[fs], entries[fs] = data, gram, entry
                save(args.output/"results.json", report)
                print(json.dumps({"finished": label, "status": entry["status"], "seconds": entry["elapsed_seconds"]}), flush=True)
            if reference_fs in loaded:
                for fs, data in loaded.items():
                    if fs != reference_fs:
                        result = {"rate": rate, "profile": profile, **sampling_fields(fs)}
                        result.update(compare(data, loaded[reference_fs], grams[fs], grams[reference_fs]))
                        result["signal_energy_ratio_to_reference"] = entries[fs]["metrics"]["signal_energy_seconds"]/entries[reference_fs]["metrics"]["signal_energy_seconds"]
                        report["comparisons"].append(result)
            elif loaded:
                report["comparisons"].append({"rate": rate, "profile": profile,
                    "reference_sample_rate": float(reference_fs), "status": "reference_unavailable"})
            for fs, data in loaded.items():
                # Close mapped files before optional unlink; retain commands,
                # manifests, logs and hashes as compact reproducibility evidence.
                data["signal"]._mmap.close()
                data["basis"]._mmap.close()
                if not args.keep_raw:
                    directory = args.output/f"rate-{rate:g}-{profile}-fs-{sampling_label(fs)}"
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
