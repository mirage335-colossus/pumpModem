"""Complete actual-observation cost adapters for the retained-state families.

These are direct measurements, not truncated likelihoods or duration models.
The receiver math and its calibration stay in the source-root study helpers.
In particular RBPF's streaming read/projection/update/finish is timed together;
its internal timers are diagnostics, never added a second time to the total.
"""
from __future__ import annotations

import math
import os
from pathlib import Path
import tempfile
import time

import numpy as np


FILES = ("manifest.json", "gram.c128", "observation.c128", "received_energy.f64",
         "intervals.u64", "valid_rows.u64")
SOURCES = {
    "rbpf": "triad-rbpf-study.py",
    "static-frequency": "triad-static-frequency-study.py",
    "ray-frequency": "triad-ray-frequency-study.py",
    "cfo-batch": "triad-cfo-batch-study.py",
}
DEFAULT_METHODS = {"rbpf": "rbpf-ucb", "static-frequency": "static-frequency-ucb",
                   "ray-frequency": "ray-frequency-ucb", "cfo-batch": "rbpf-ucb"}


def _plain(value):
    if isinstance(value, dict):
        return {str(k): _plain(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_plain(v) for v in value]
    if isinstance(value, np.ndarray):
        return _plain(value.tolist())
    if isinstance(value, np.generic):
        return _plain(value.item())
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, float) and not math.isfinite(value):
        return str(value)
    return value


def _arguments(module, family, paths, options, device, precision):
    options = dict(options)
    if set(options) & {"inputs", "output", "device", "precision"}:
        raise ValueError("inputs/output/device/precision belong to the adapter interface")
    # Retain the maintained command's validation and its declared family budget.
    options.setdefault("methods", [DEFAULT_METHODS[family]])
    if family in ("static-frequency", "ray-frequency"):
        options.setdefault("cn0", 0.)
    if "reference_variance" not in options and "cn0" not in options:
        raise ValueError("declare reference_variance or cn0 in case.options")
    with tempfile.TemporaryDirectory(prefix="triad-family-args-") as temp:
        argv = [str(p) for p in paths] + ["--output", str(Path(temp)/"unused.json"),
                "--device", device, "--precision", precision]
        for key, value in options.items():
            if not isinstance(key, str) or not key or key.startswith("-"):
                raise ValueError("invalid receiver option name")
            if value is None:
                continue
            argv.append("--" + key.replace("_", "-"))
            argv.extend(str(v) for v in value) if isinstance(value, (list, tuple)) else argv.append(str(value))
        try:
            return module.parse_args(argv)
        except SystemExit as error:
            raise ValueError("invalid maintained receiver options") from error


def _sync(torch, device):
    if device.startswith("cuda"):
        torch.cuda.synchronize(device)


def _thread_regime(torch):
    """Observe settings without changing the controller's process allocation."""
    try:
        from threadpoolctl import threadpool_info
    except ImportError:
        pools, inspection = [], "unavailable: threadpoolctl is not installed; NumPy/SciPy effective pool sizes are unobserved"
    else:
        pools, inspection = threadpool_info(), "threadpoolctl runtime query"
    return {"torch_intraop": torch.get_num_threads(), "torch_interop": torch.get_num_interop_threads(),
            "torch_parallel_info": torch.__config__.parallel_info(), "library_pools": pools,
            "library_pool_inspection": inspection,
            "environment": {name: os.environ.get(name) for name in
                ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS", "BLIS_NUM_THREADS",
                 "VECLIB_MAXIMUM_THREADS", "NUMEXPR_NUM_THREADS")},
            "scope": "Observed Torch and available library runtime settings; environment values are controls, not proof of effective pool sizes"}


def _planned_count(module, family, workspaces, args):
    """Independent full target from public metadata and declared settings."""
    total = 0
    for ws in workspaces:
        _, mapping, _ = _mapping(ws, 2 if family == "ray-frequency" else args.taps)
        if family in ("rbpf", "cfo-batch"):
            total += ws.block_count * len(mapping) * args.particles * len(args.methods)
        elif family == "static-frequency":
            _, half = module.grid_shape(ws.block_count, ws.block_samples/ws.sample_rate,
                ws.sample_count/ws.sample_rate, args.frequency_bound, args.frequency_step)
            total += (2*half+1) * len(mapping) * len(args.methods)
        else:
            _, common, relative = module.grid_shape(ws.block_count, ws.block_samples/ws.sample_rate,
                ws.sample_count/ws.sample_rate, args.frequency_bound, args.ray_frequency_bound, args.frequency_step)
            # The maintained relative prior includes every bin unless all mass
            # is explicitly assigned to the zero-frequency atom.
            active = 1 if args.zero_ray_mass == 1. else 2*relative+1
            total += (2*common+1) * active * len(mapping) * len(args.methods)
    return total


def _timed(seconds, name, function, torch, device):
    _sync(torch, device)
    start = time.perf_counter()
    value = function()
    _sync(torch, device)
    seconds[name] += time.perf_counter() - start
    return value


def _mapping(ws, taps):
    indices = [i for i, columns in enumerate(ws.column_mapping) if len(columns) == taps]
    mapping = np.asarray([ws.column_mapping[i] for i in indices], dtype=np.int64)
    labels = np.asarray([ws.candidates[i]["bit"] for i in indices], dtype=np.int64)
    if mapping.ndim != 2 or set(labels.tolist()) != {0, 1}:
        raise ValueError("bank must contain both bits at the requested tap dimension")
    return indices, mapping, labels


def _reference(ws, args):
    value = args.reference_variance
    if value is None:
        value = ws.sample_rate / 10**(args.cn0 / 10)
    if not math.isfinite(value) or value <= 0:
        raise ValueError("receiver reference variance is unresolved")
    return value


def _geometry(ws, indices, mapping):
    return {"blocks": ws.block_count, "samples": ws.sample_count,
            "block_samples": ws.block_samples, "sample_rate": ws.sample_rate,
            "full_bank_dimension": ws.dimension, "selected_candidate_indices": indices,
            "candidate_mapping": mapping.tolist(), "candidate_count": len(mapping),
            "taps": mapping.shape[1], "input_bytes": sum((ws.directory/n).stat().st_size for n in FILES),
            "statistics_bytes": sum((ws.directory/n).stat().st_size for n in FILES[1:])}


def _finish(ws, scores, classify, threshold):
    # Simulation truth is evaluation-only and is accessed after the scores exist.
    control = ws.manifest.get("control", "signal")
    truth = None
    if control == "signal":
        truth = ws.manifest.get("signal_bit_simulation_truth", ws.manifest.get("signal_bit", 0))
        if isinstance(truth, bool) or truth not in (0, 1):
            raise ValueError("signal evaluation truth must be binary")
    winner = int(np.argmax(scores[0]))
    return {"scores": scores[0].tolist(), "admitted": bool(scores[0, winner] >= threshold),
            "winner": winner, "threshold": threshold, "control": control,
            "counts": classify(scores, truth, threshold, control)}


def _rbpf(module, ws, args, index, timing, torch):
    indices, mapping, labels = module.candidate_mapping(ws, args.taps)
    reference = _reference(ws, args)
    upper = calibration = None
    if "rbpf-ucb" in args.methods:
        upper, calibration = _timed(timing, "prepare", lambda: module.calibrate(
            ws, reference, args.budget.delta_per_segment), torch, args.device)
    results, internal = _timed(timing, "transfer_and_score", lambda: module.infer(
        ws, mapping, labels, reference, args, upper, (args.seed + index) % 2**63), torch, args.device)
    rows = {}
    for method, value in results.items():
        rows[method] = _timed(timing, "finish", lambda: _finish(
            ws, value["scores"], module.classify, args.budget.log_threshold), torch, args.device)
        rows[method]["precision_diagnostics"] = value["diagnostics"]
    geometry = _geometry(ws, indices, mapping)
    events = sum(v["diagnostics"]["resampling_events_total"] for v in results.values())
    executed = sum(v["diagnostics"]["blocks"] * v["diagnostics"]["particles"] *
                   math.prod(v["diagnostics"]["leading_shape"]) for v in results.values())
    geometry.update(particles=args.particles, methods=len(args.methods), resampling_events=events,
                    particle_candidate_block_updates=executed,
                    full_bank_projection_blocks=ws.block_count * (1 + int(upper is not None)),
                    statistics_read_bytes=geometry["statistics_bytes"] * (1 + int(upper is not None)),
                    owned_state_bytes=sum(v["diagnostics"]["owned_state_bytes"] for v in results.values()))
    return rows, geometry, {"calibration": calibration, "receiver_internal_seconds": internal,
                            "combined_stage": "transfer_and_score includes streaming input reads, full-bank projection, transfer, updates and receiver finish"}


def _static(module, ws, args, timing, torch):
    indices, mapping, labels = _mapping(ws, args.taps)
    dt = ws.block_samples/ws.sample_rate
    nfft, half = module.grid_shape(ws.block_count, dt, ws.sample_count/ws.sample_rate,
                                    args.frequency_bound, args.frequency_step)
    bound = module.memory_bound(ws.block_count, len(mapping), args.taps, ws.dimension,
                                 nfft, 2*half+1, args.precision)
    if bound > args.memory_limit_bytes:
        raise MemoryError("complete static frequency bank exceeds memory limit")
    bins = np.arange(-half, half+1, dtype=np.int64)
    raw, projected, grams, centers, upper, calibration = _timed(timing, "prepare", lambda: module.prepare(
        ws, mapping, _reference(ws, args), args.budget.delta_per_segment), torch, args.device)
    rows, executed = {}, []
    for method in args.methods:
        ucb = method.endswith("-ucb")
        diagnostics = {}
        white, determinant = _timed(timing, "prepare", lambda: module.whiten_channel(
            projected/upper if ucb else raw, grams[int(ucb)]/upper if ucb else grams[0],
            diagnostics=diagnostics), torch, args.device)
        def score():
            correlations = module.frequency_correlations(white, centers, dt, nfft, bins,
                implementation=args.implementation, device=args.device, precision=args.precision)
            executed.append(int(correlations.shape[0] * correlations.shape[1]))
            return module.scores_from_correlations(correlations, determinant, labels)
        scores = _timed(timing, "transfer_and_score", score, torch, args.device)
        rows[method] = _timed(timing, "finish", lambda: _finish(
            ws, scores, module.classify, args.budget.log_threshold), torch, args.device)
        rows[method]["precision_diagnostics"] = diagnostics
    geometry = _geometry(ws, indices, mapping)
    geometry.update(nfft=nfft, frequency_bins=len(bins), frequency_endpoints_hz=[-half/(nfft*dt), half/(nfft*dt)],
                    methods=len(args.methods), fft_transforms=len(mapping)*args.taps*len(args.methods) if args.implementation == "fft" else 0,
                    fft_elements=nfft*len(mapping)*args.taps*len(args.methods) if args.implementation == "fft" else 0,
                    frequency_candidate_scores=sum(executed),
                    direct_frequency_tile=64 if args.implementation == "direct" else None,
                    full_bank_projection_blocks=ws.block_count, statistics_read_bytes=geometry["statistics_bytes"],
                    memory_bound_bytes=bound)
    return rows, geometry, {"calibration": calibration}


def _ray(module, ws, args, timing, torch):
    indices, mapping, labels = _mapping(ws, 2)
    dt = ws.block_samples/ws.sample_rate
    nfft, fh, dh = module.grid_shape(ws.block_count, dt, ws.sample_count/ws.sample_rate,
        args.frequency_bound, args.ray_frequency_bound, args.frequency_step)
    bounds = module.memory_bound(ws.block_count, len(mapping), ws.dimension, nfft, fh, dh, args)
    effective = args.memory_limit_bytes
    if args.device.startswith("cuda"):
        effective = min(effective, int(.75*torch.cuda.mem_get_info(args.device)[0]))
    if bounds["total_array_bound_bytes"] > effective:
        raise MemoryError("complete ray frequency bank exceeds effective memory limit")
    fbins, dbins = np.arange(-fh, fh+1, dtype=np.int64), np.arange(-dh, dh+1, dtype=np.int64)
    rr, gg, centers, upper, calibration = _timed(timing, "prepare", lambda: module.prepare(
        ws, mapping, _reference(ws, args), args.budget.delta_per_segment, args.projection_tile_blocks), torch, args.device)
    rows, receiver_timings, executed = {}, {}, []
    for method in args.methods:
        ucb = method.endswith("-ucb")
        def score():
            return module.joint_evidence(rr[int(ucb)]/(upper if ucb else 1.),
                gg[int(ucb)]/(upper if ucb else 1.), centers, dt, nfft, fbins, dbins,
                zero_mass=args.zero_ray_mass, device=args.device, precision=args.precision,
                frequency_tile=args.frequency_tile, ray_tile=args.ray_frequency_tile)
        candidate, diagnostics = _timed(timing, "transfer_and_score", score, torch, args.device)
        executed.append(diagnostics["evaluated_frequency_pairs_per_candidate"] * candidate.numel())
        def finish():
            return _finish(ws, module.mix_candidates(candidate, labels), module.adapter.classify, args.budget.log_threshold)
        rows[method] = _timed(timing, "finish", finish, torch, args.device)
        receiver_timings[method] = diagnostics
    active = int(np.count_nonzero(module.ray_weights(dbins, args.zero_ray_mass)))
    geometry = _geometry(ws, indices, mapping)
    geometry.update(nfft=nfft, common_bins=len(fbins), relative_bins=len(dbins), active_relative_bins=active,
                    combined_correlation_bins=2*(fh+dh)+1, methods=len(args.methods),
                    frequency_candidate_scores=sum(executed),
                    fft_transforms=3*len(mapping)*len(args.methods), fft_elements=nfft*3*len(mapping)*len(args.methods),
                    frequency_tile=args.frequency_tile, ray_frequency_tile=args.ray_frequency_tile,
                    joint_tiles=math.ceil(len(fbins)/args.frequency_tile)*math.ceil(active/args.ray_frequency_tile)*len(args.methods),
                    projection_tile_blocks=min(ws.block_count, args.projection_tile_blocks),
                    full_bank_projection_blocks=ws.block_count, statistics_read_bytes=geometry["statistics_bytes"],
                    memory_bounds=bounds)
    return rows, geometry, {"calibration": calibration, "receiver_internal_seconds": receiver_timings}


def _cfo(module, workspaces, args, timing, torch):
    signatures = [module.geometry_signature(ws) for ws in workspaces]
    if any(signature != signatures[0] for signature in signatures[1:]):
        raise ValueError("CFO batch geometry/candidate-column signature mismatch")
    captures, geometries, batches = [], [], []
    for first in range(0, len(workspaces), args.batch_size):
        batch = workspaces[first:first+args.batch_size]
        ws = batch[0]
        indices, mapping, labels = module.adapter.candidate_mapping(ws, args.taps)
        bounds = module.memory_preflight(ws.block_count, len(batch), len(mapping), args.taps,
            ws.dimension, args.particles, args.precision, args.projection_tile_blocks)
        effective = args.memory_limit_bytes
        if args.device.startswith("cuda"):
            effective = min(effective, int(.75*torch.cuda.mem_get_info(args.device)[0]))
        if bounds["total_array_bound_bytes"] > effective:
            raise MemoryError("complete CFO batch exceeds effective memory limit")
        rb, gb, elapsed, metadata = _timed(timing, "prepare", lambda: module.prepare_batch(batch, mapping, args), torch, args.device)
        state_limit = effective - bounds["host_device_conversion_bound_bytes"] - bounds["residual_preparation_bound_bytes"]
        candidate, diagnostics, internal = _timed(timing, "transfer_and_score", lambda: module.score_batch(
            rb, gb, elapsed, args, state_limit), torch, args.device)
        if tuple(diagnostics["leading_shape"]) != (len(batch), len(mapping)):
            raise ValueError("executed CFO batch dimensions differ from the full public target")
        for i, current in enumerate(batch):
            def finish():
                scores = module.adapter.mix_candidates(torch.from_numpy(candidate[i]), labels)
                return _finish(current, scores, module.adapter.classify, args.budget.log_threshold)
            row = _timed(timing, "finish", finish, torch, args.device)
            row["precision_diagnostics"] = diagnostics
            captures.append({"methods": {module.METHOD: row}, **metadata[i]})
            geometry = _geometry(current, indices, mapping)
            geometry.update(particles=args.particles, methods=1, full_bank_projection_blocks=current.block_count,
                statistics_read_bytes=geometry["statistics_bytes"],
                particle_candidate_block_updates=diagnostics["blocks"]*diagnostics["leading_shape"][1]*diagnostics["particles"])
            geometries.append(geometry)
        batches.append({"batch_count": len(batch), "rbank_shape": list(rb.shape), "gbank_shape": list(gb.shape),
                        "resident_bank_bytes": rb.nbytes+gb.nbytes+elapsed.nbytes,
                        "diagnostics": diagnostics, "receiver_internal_seconds": internal, "memory_bounds": bounds})
    return captures, geometries, batches


def run_case(root: Path, fixture: Path | tuple[Path, ...], case: dict, device: str,
             precision: str, limit: int | None = None) -> dict:
    """Measure every observation block and every declared alternative once."""
    if limit is not None:
        raise ValueError("this family supports full direct measurements only; limit must be None")
    family = case.get("family")
    if family not in SOURCES:
        raise ValueError("unsupported retained-state family")
    if precision not in ("fp32", "fp64") or not (device == "cpu" or device.startswith("cuda")):
        raise ValueError("unsupported device or precision")
    start = time.perf_counter()
    from triad_perf_families import load_source
    module = load_source(Path(root), SOURCES[family])
    torch = module.torch
    if device.startswith("cuda"):
        if not torch.cuda.is_available():
            raise ValueError("CUDA unavailable")
        torch.cuda.init()
        torch.cuda.set_device(device)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.set_float32_matmul_precision("highest")
    paths = tuple(Path(p).resolve(strict=True) for p in fixture) if isinstance(fixture, tuple) else (Path(fixture).resolve(strict=True),)
    if not paths or len(set(paths)) != len(paths):
        raise ValueError("fixtures must be distinct physical paths")
    options = dict(case.get("options", {}))
    controller_threads = torch.get_num_threads()
    if options.get("threads") is not None and options["threads"] != controller_threads:
        raise ValueError("receiver threads contradict the controller's actual Torch thread setting")
    options["threads"] = controller_threads
    args = _arguments(module, family, paths, options, device, precision)
    threads = _thread_regime(torch)
    workspaces = [module.BlockWorkspace(p, max_tile_blocks=1, memory_limit_bytes=args.memory_limit_bytes) for p in paths]
    seen = set()
    for ws in workspaces:
        if not ws.has_observation or not ws.has_received_energy or not ws.gaussian_projection_supported:
            raise ValueError("actual observations, received energy and Gaussian-supported frontend required")
        if ws.manifest.get("control", "signal") not in ("signal", "null", "wrong-key"):
            raise ValueError("unsupported actual control")
        identity = tuple(ws.manifest.get(k) for k in ("channel_seed", "key_seed", "epoch"))
        if all(v is not None for v in identity):
            if identity in seen:
                raise ValueError("duplicate channel/key/epoch physical identity")
            seen.add(identity)
    target_count = _planned_count(module, family, workspaces, args)
    timing = {"load": time.perf_counter()-start, "prepare": 0., "transfer_and_score": 0., "finish": 0.}
    captures, geometries, batches = [], [], []
    if family == "cfo-batch":
        captures, geometries, batches = _cfo(module, workspaces, args, timing, torch)
    else:
        for index, ws in enumerate(workspaces):
            if family == "rbpf":
                rows, geometry, metadata = _rbpf(module, ws, args, index, timing, torch)
            else:
                rows, geometry, metadata = (_static if family == "static-frequency" else _ray)(module, ws, args, timing, torch)
            captures.append({"methods": rows, **metadata})
            geometries.append(geometry)
    count_key = "particle_candidate_block_updates" if family in ("rbpf", "cfo-batch") else "frequency_candidate_scores"
    executed_count = sum(g[count_key] for g in geometries)
    if executed_count != target_count:
        raise ValueError(f"executed work {executed_count} differs from full public target {target_count}")
    results = [row for capture in captures for row in capture["methods"].values()]
    settings = {k: v for k, v in vars(args).items() if k not in ("inputs", "output", "budget", "tap_prior", "phase_prior")}
    regime_geometry = [{k: v for k, v in g.items() if k not in ("input_bytes", "statistics_bytes", "statistics_read_bytes", "resampling_events", "owned_state_bytes")} for g in geometries]
    report = _plain({"seconds": timing,
        "work": {"unit": count_key, "count": executed_count, "target_count": target_count,
                 "complete": True, "captures": len(captures), "blocks": sum(g["blocks"] for g in geometries),
                 "input_bytes": sum(g["input_bytes"] for g in geometries),
                 "statistics_read_bytes": sum(g["statistics_read_bytes"] for g in geometries),
                 "resampling_events": sum(g.get("resampling_events", 0) for g in geometries),
                 "geometries": geometries, "batches": batches,
                 "counter_provenance": {
                     "target_count": "public complete workspace metadata and declared particles/methods or full frequency grid",
                     "count": {"rbpf": "executed receiver blocks, particles and leading-shape diagnostics",
                               "cfo-batch": "executed receiver blocks, particles and checked batch leading-shape diagnostics",
                               "static-frequency": "returned frequency-correlation array dimensions for every scored method",
                               "ray-frequency": "receiver evaluated-frequency-pair diagnostics times returned candidate elements"}[family],
                     "resampling_events": "instrumented receiver resampling-event diagnostics; CFO resampling is disabled",
                     "other_geometry": "derived public/array geometry and memory bounds, not hardware event counters"}},
        "regime": {"family": family, "mode": "full-direct", "geometry": regime_geometry,
                   "settings": settings, "threads": threads, "batch_shapes": [b["rbank_shape"] for b in batches],
                   "storage_dtype": "complex128", "bulk_dtype": "complex64" if precision == "fp32" else "complex128",
                   "preparation_dtype": "float64/complex128", "sensitive_reduction_dtype": "float64/complex128",
                   "tf32_allowed": False},
        "result": {"scores": [r["scores"] for r in results], "admissions": [r["admitted"] for r in results],
                   "captures": captures, "family_budget": args.budget.diagnostics(),
                   "precision_diagnostics": [r.get("precision_diagnostics", {}) for r in results]}})
    # The stage intervals are disjoint. Charge everything outside them,
    # including mapping/grid/preflight and JSON-compatible report construction,
    # to an explicitly unattributed measured residual, never a guessed stage.
    measured = time.perf_counter()-start
    report["seconds"]["unattributed"] = measured-sum(timing.values())
    report["result"]["timing_accounting"] = {
        "adapter_wall_seconds": measured,
        "scope": "Post-interface-validation through JSON-compatible report construction; excludes these final receipt writes and return overhead, which the outer worker wall measures",
        "unattributed_scope": "Mapping, grid/preflight, bookkeeping, synchronization outside stage intervals and report construction; disjoint residual of measured adapter wall minus named stages"}
    return report
