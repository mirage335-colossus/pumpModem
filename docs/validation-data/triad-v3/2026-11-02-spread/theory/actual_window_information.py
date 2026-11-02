#!/usr/bin/env python3
"""Public sampled-design information diagnostic; no noisy observations or truth.

This computes presence KL for the declared independent-window, block-center
proper-complex Gaussian alternative, with known stationary white-noise variance.
It is not the KL of the physical continuous/FIR channel or a reception forecast.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np
from scipy.optimize import brentq

REPOSITORY = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(REPOSITORY / "tools"))
from triad_gaussian_window import prepare_windowed_gaussian
from spread_frontier import profiles, upper_power_given_kl


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for part in iter(lambda: source.read(1024*1024), b""):
            h.update(part)
    return h.hexdigest()


def positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + " must be a positive integer")
    return value


def finite(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(name + " must be finite")
    return float(value)


def gaussian_kl_terms(x):
    """x-log1p(x), using a convergent series to avoid low-SNR cancellation."""
    x = np.asarray(x, dtype=np.float64)
    if not np.isfinite(x).all() or np.any(x < 0):
        raise ValueError("covariance eigenvalues over variance must be finite nonnegative")
    output = np.empty_like(x)
    small = x < 1e-3
    z = x[small]
    polynomial = np.full_like(z, 1/12)
    for order in range(11, 1, -1):
        polynomial = (-1.)**order/order + z*polynomial
    output[small] = z*z*polynomial
    output[~small] = x[~small] - np.log1p(x[~small])
    return output


def presence_kl(eigenvalues, sample_rate, cn0_db_hz):
    """Exact model formula, numerically evaluated in FP64 (not interval certified)."""
    rate = finite(sample_rate, "sample rate")
    cn0 = finite(cn0_db_hz, "C/N0")
    if rate <= 0 or not -300 < cn0 < 300:
        raise ValueError("sample rate must be positive and C/N0 inside (-300,300)")
    variance = rate / 10**(cn0/10)
    return float(np.sum(gaussian_kl_terms(np.asarray(eigenvalues)/variance), dtype=np.float64))


def necessary_cn0(eigenvalues, sample_rate, target_kl, low=-200., high=100.):
    """Invert the necessary information inequality, never an achievable threshold."""
    if not -300 < low < high < 300 or not math.isfinite(target_kl) or target_kl <= 0:
        raise ValueError("invalid inversion interval or KL target")
    values = np.asarray(eigenvalues, dtype=np.float64)
    if not np.isfinite(values).all() or np.any(values < 0):
        raise ValueError("invalid covariance spectrum")
    if not np.any(values):
        return {"necessary_cn0_db_hz": None, "status": "zero signal covariance; target impossible in this model"}
    equation = lambda c: presence_kl(values, sample_rate, c)-target_kl
    if equation(low) > 0:
        return {"necessary_cn0_db_hz": None, "status": "crossing below declared inversion interval"}
    if equation(high) < 0:
        return {"necessary_cn0_db_hz": None, "status": "crossing above declared inversion interval"}
    crossing = float(brentq(equation, low, high, xtol=1e-10, rtol=1e-13))
    return {"necessary_cn0_db_hz": crossing, "status": "resolved necessary crossing; not sufficient",
            "KL_at_crossing_nats": presence_kl(values, sample_rate, crossing)}


def load_public_design(directory, taps, maximum_window, memory_limit_bytes):
    """Read only manifest public fields, full G, intervals and valid-row counts.

    Other manifest fields (including noise variance, signal bit and channel seed)
    do not affect calculation. No observation/mean/energy file is opened or needed.
    """
    directory = Path(directory)
    manifest = json.loads((directory/"manifest.json").read_text())
    representation = {"format": "triad-block-stats-v1", "byte_order": "little",
        "complex_type": "complex128", "real_type": "float64", "integer_type": "uint64",
        "layout": "block,row,column"}
    if any(manifest.get(k) != v for k, v in representation.items()):
        raise ValueError("unsupported public statistics representation")
    if manifest.get("gaussian_projection_supported", True) is not True:
        raise ValueError("public design does not support additive white Gaussian projection")
    b, d, count, block = [positive_integer(manifest.get(k), k) for k in
                         ("block_count", "dimension", "sample_count", "block_samples")]
    fs = finite(manifest.get("sample_rate"), "sample rate")
    if fs <= 0 or b != (count+block-1)//block or count > 2**53:
        raise ValueError("invalid sample rate/calendar or FP64 timestamp extent")
    candidates, mappings = manifest.get("candidates"), manifest.get("column_mapping")
    if not isinstance(candidates, list) or not isinstance(mappings, list) or len(candidates) != len(mappings) or len(candidates) != manifest.get("candidate_count"):
        raise ValueError("invalid candidate metadata/mapping")
    selected, public_candidates, covered = [], [], set()
    for index, (candidate, mapping) in enumerate(zip(candidates, mappings)):
        if not isinstance(mapping, list) or not mapping or any(isinstance(x, bool) or not isinstance(x, int) or not 0 <= x < d for x in mapping) or len(set(mapping)) != len(mapping):
            raise ValueError("invalid public column mapping")
        covered.update(mapping)
        if not isinstance(candidate, dict) or candidate.get("tap_count") != len(mapping):
            raise ValueError("candidate tap count disagrees with mapping")
        if len(mapping) != taps:
            continue
        if isinstance(candidate.get("bit"), bool) or candidate.get("bit") not in (0, 1):
            raise ValueError("candidate bit label must be zero or one")
        public = {"source_candidate_index": index, "bit": candidate["bit"], "columns": mapping}
        for name in ("epoch_offset_seconds", "arrival_seconds", "frequency_hz", "clock_ppm", "frequency_drift_hz_per_second"):
            public[name] = finite(candidate.get(name), "candidate " + name)
        if not isinstance(candidate.get("taps"), list) or len(candidate["taps"]) != taps:
            raise ValueError("candidate public tap metadata is malformed")
        public["taps"] = [{name: finite(tap.get(name), "tap " + name) for name in
                           ("delay_seconds", "doppler_hz")} for tap in candidate["taps"]]
        selected.append(mapping)
        public_candidates.append(public)
    if not selected or covered != set(range(d)):
        raise ValueError("no matching candidates or incomplete full-Gram mapping")
    c = len(selected)
    w = min(b, positive_integer(maximum_window, "window blocks"))
    if taps > 16 or w*taps > 2048:
        raise ValueError("window dimension exceeds prepared model limit")
    full, remainder = divmod(b, maximum_window)
    prepared_bound = c*(full*maximum_window**2+remainder**2)*taps*taps*16+c*b*taps*8
    prepared_bound += 6*b*c*taps*taps*16+24*(w*taps)**2*16+b*8
    # Full public G, selected G, calendars, full-G validation temporaries and
    # hash buffer are counted in addition to the existing prepared-plan bound.
    reader_bound = b*d*d*16+b*c*taps*taps*16+b*64+8*d*d*16+2*1024*1024
    if reader_bound+prepared_bound > memory_limit_bytes:
        raise MemoryError("public design plus window preparation exceeds explicit array bound")
    files = {"gram.c128": ("gram_file", "gram_shape", [b,d,d], "<c16"),
             "intervals.u64": ("intervals_file", "intervals_shape", [b,2], "<u8"),
             "valid_rows.u64": ("valid_rows_file", "valid_rows_shape", [b], "<u8")}
    arrays = {}
    for name, (filekey, shapekey, shape, dtype) in files.items():
        if manifest.get(filekey) != name or manifest.get(shapekey) != shape:
            raise ValueError("invalid public file layout: " + name)
        expected = math.prod(shape)*np.dtype(dtype).itemsize
        if (directory/name).stat().st_size != expected:
            raise ValueError("incorrect public file size: " + name)
        arrays[name] = np.fromfile(directory/name, dtype=dtype).reshape(shape)
    G, intervals, valid = (arrays[n] for n in files)
    starts = np.arange(b, dtype=np.uint64)*block
    stops = np.minimum(starts+block, count)
    if not np.array_equal(intervals[:,0], starts) or not np.array_equal(intervals[:,1], stops) or np.any(valid > stops-starts):
        raise ValueError("invalid public interval/valid-row calendar")
    max_full_gram_negative = 0.
    for index, gram in enumerate(G):
        if not np.isfinite(gram).all() or (not valid[index] and np.any(gram != 0)):
            raise ValueError("nonfinite Gram or nonzero Gram in erased block")
        scale = max(float(np.max(np.abs(gram))), np.finfo(float).tiny)
        if np.max(np.abs(gram-gram.conj().T)) > 1e-10*scale:
            raise ValueError("full public Gram is not Hermitian")
        eigenvalues = np.linalg.eigvalsh((gram+gram.conj().T)*.5)
        if eigenvalues[0] < -1e-10*scale:
            raise ValueError("full public Gram is not positive semidefinite")
        max_full_gram_negative = max(max_full_gram_negative, max(0., -float(eigenvalues[0]))/scale)
    mapping = np.asarray(selected, dtype=np.int64)
    grams = G[:, mapping[:,:,None], mapping[:,None,:]].copy()
    centers = (intervals[:,0].astype(np.float64)+intervals[:,1].astype(np.float64))/(2*fs)
    if not np.isfinite(centers).all() or np.any(np.diff(centers) <= 0):
        raise ValueError("unresolved public physical centers")
    metadata = {"workspace": str(directory), "sample_rate": fs, "sample_count": count,
        "observation_seconds": count/fs, "block_samples": block, "blocks": b,
        "valid_sample_count": int(sum(int(v) for v in valid)), "candidates": public_candidates,
        "maximum_full_gram_relative_negative_eigenvalue": max_full_gram_negative,
        "public_source_sha256": {name: sha256(directory/name) for name in ("manifest.json", *files)},
        "explicit_array_preflight_bytes": reader_bound+prepared_bound,
        "memory_scope": "includes full public Gram, selected candidate Grams, calendars and prepared-plan explicit arrays; Python/framework/LAPACK overhead excluded"}
    return grams, centers, metadata


def analyze(directory, cn0_grid, window_blocks, width_hz, powers=(.5,.5),
            memory_limit_bytes=256*1024*1024, inversion=(-200.,100.), comparison_cn0=None):
    grams, centers, metadata = load_public_design(directory, len(powers), max(window_blocks), memory_limit_bytes)
    results = []
    for window in window_blocks:
        plan = prepare_windowed_gaussian(grams, centers, doppler_width_hz=width_hz,
            window_blocks=window, stationary_power=powers, memory_limit_bytes=memory_limit_bytes)
        spectra = [np.concatenate([w.eigenvalues[c] for w in plan._windows]) for c in range(plan.candidates)]
        details = plan.diagnostics()
        del plan
        candidate_rows = []
        for candidate, spectrum in zip(metadata["candidates"], spectra):
            crossings = []
            for profile in profiles():
                crossing = necessary_cn0(spectrum, metadata["sample_rate"], profile["required_binary_KL_nats"], *inversion)
                if comparison_cn0 is not None and crossing["necessary_cn0_db_hz"] is not None:
                    crossing["comparison_minus_necessary_cn0_db"] = comparison_cn0-crossing["necessary_cn0_db_hz"]
                crossings.append({"profile": profile["id"], **crossing})
            rows = []
            for cn0 in cn0_grid:
                kl = presence_kl(spectrum, metadata["sample_rate"], cn0)
                rows.append({"cn0_db_hz": cn0, "known_noise_variance": metadata["sample_rate"]/10**(cn0/10),
                    "presence_KL_nats": kl,
                    "targets": [{"profile": p["id"], "necessary_information_condition_met": kl >= p["required_binary_KL_nats"],
                         "presence_probability_upper_from_binary_KL": upper_power_given_kl(kl, p["false_probability_used_in_necessity"])} for p in profiles()]})
            candidate_rows.append({"candidate": candidate, "covariance_eigenvalue_count": len(spectrum),
                "covariance_trace_reference_variance_one": float(spectrum.sum()),
                "covariance_eigenvalues_sha256": hashlib.sha256(spectrum.astype("<f8").tobytes()).hexdigest(),
                "cn0_evaluations": rows, "necessary_cn0_crossings": crossings})
        results.append({"window_blocks": window, "preparation": details, "candidate_results": candidate_rows})
    return {"public_design": metadata, "window_results": results}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", action="append", required=True)
    parser.add_argument("--cn0-grid", required=True, help="predeclared comma-separated C/N0 dB-Hz; use = for a leading minus")
    parser.add_argument("--window-blocks", type=int, nargs="+", default=[16])
    parser.add_argument("--doppler-width", type=float, required=True, help="Gaussian spectral width 2sigma Hz, not ray-center separation")
    parser.add_argument("--tap-powers", default="0.5,0.5")
    parser.add_argument("--memory-mib", type=int, default=256)
    parser.add_argument("--inversion-db-min", type=float, default=-200.)
    parser.add_argument("--inversion-db-max", type=float, default=100.)
    parser.add_argument("--comparison-cn0-db", type=float, help="optional externally supplied measured/reference value; no measurement claim made here")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    cn0 = [float(x) for x in args.cn0_grid.split(",")]
    powers = tuple(float(x) for x in args.tap_powers.split(","))
    if not cn0 or len(set(cn0)) != len(cn0) or any(not math.isfinite(c) or not -300<c<300 for c in cn0):
        parser.error("C/N0 grid must be distinct finite numbers in (-300,300)")
    if not powers or len(powers)>16 or any(not math.isfinite(p) or p<0 for p in powers) or not math.isclose(sum(powers), 1., rel_tol=0, abs_tol=1e-12):
        parser.error("tap powers must be nonnegative and sum to one for total-power C/N0")
    if len(set(args.window_blocks)) != len(args.window_blocks) or any(not 1<=w<=1024 for w in args.window_blocks):
        parser.error("window blocks must be distinct in 1..1024")
    if not math.isfinite(args.doppler_width) or args.doppler_width<0 or not -300<args.inversion_db_min<args.inversion_db_max<300 or args.memory_mib<1:
        parser.error("invalid width, inversion interval or memory bound")
    if args.comparison_cn0_db is not None and not math.isfinite(args.comparison_cn0_db):
        parser.error("comparison C/N0 must be finite")
    report = {"format": "triad-public-window-information-v1",
        "scope": [
            "Exact covariance-spectrum KL formula evaluated in FP64 for the declared independent-window block-center Gaussian alternative; no interval-certified numeric bounds.",
            "Known candidate timing/epoch/frequency/tap design and known stationary white noise; physical FIR intra-block evolution and cross-window correlation are not modeled by this alternative.",
            "Only public manifest fields, full sampled Grams, intervals and valid-row calendars enter; no mean, signal energy, observed noise, injected variance, true bit or physical fading realization is used.",
            "For each candidate D(P_signal||P_null)=sum(lambda/variance-log1p(lambda/variance)); variance=sample_rate/10^(total_nominal_triplet_CN0/10). Actual shaped energy and public erasures enter through G.",
            "Binary KL thresholds are necessary presence conditions, not sufficient correct-bit performance, empirical false-admission calibration or a duration recommendation. Bit discrimination and search uncertainty are separate losses.",
            "Whole-horizon alpha gives a universal necessary condition within this declared model; minute/model/duration/bit allocation is conditional on that chosen allocation, not necessary for every family-wise detector.",
            "Any comparison C/N0 is supplied externally and is not validated here. Its difference from a necessary crossing is not a proved receiver inefficiency or an achievable improvement."],
        "target_profiles": profiles(), "cn0_grid_db_hz": cn0, "doppler_width_2sigma_hz": args.doppler_width,
        "tap_powers": powers, "inversion_interval_db_hz": [args.inversion_db_min,args.inversion_db_max],
        "comparison_cn0_db_hz_external": args.comparison_cn0_db,
        "source_sha256": {str(p.relative_to(REPOSITORY)): sha256(p) for p in
            (Path(__file__).resolve(), Path(__file__).with_name("spread_frontier.py"), REPOSITORY/"tools/triad_gaussian_window.py")},
        "workspaces": [analyze(w, cn0, args.window_blocks, args.doppler_width, powers,
            args.memory_mib*1024*1024, (args.inversion_db_min,args.inversion_db_max), args.comparison_cn0_db) for w in args.workspace]}
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")


if __name__ == "__main__":
    main()
