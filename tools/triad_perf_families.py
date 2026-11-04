#!/usr/bin/env python3
"""Source-bound sampled performance qualification for isolated receiver families.

The cold controller reports worker startup separately. The complete warm-worker
target starts a fresh native exporter, loads its complete sampled workspace,
executes the declared receiver, and receives the finished result over JSONL.
Fixture hashing and evidence writes are measured as qualification overhead.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import importlib
import importlib.metadata
import importlib.util
import json
import math
import os
from pathlib import Path
import runpy
import statistics
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path(__file__).with_name("triad-performance-families.json")
SCHEMA = "triad-performance-sampled-families-v1"
WORKER = Path(__file__).resolve()
MEASUREMENT_DEPENDENCIES = ("triad_perf_families.py", "triad_perf_adaptive.py",
                            "triad_perf_hardware.py", "triad-performance.py")
STATE = ("gaussian-state", "phase-reference", "phase-optimized", "phase-graph", "phase-reduction", "phase-tensor")
TRITON_PHASE = STATE[2:]
OTHER = ("rbpf", "static-frequency", "ray-frequency", "cfo-batch")
ADAPTERS = {**dict.fromkeys(STATE, "triad_perf_family_state"),
            **dict.fromkeys(OTHER, "triad_perf_family_other"),
            "grouped-acquisition": "triad_perf_family_group"}


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    allow_nan=False).encode()).hexdigest()


def file_sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    with Path(path).open("x") as out:
        json.dump(value, out, indent=2, allow_nan=False)
        out.write("\n")


def manifest():
    value = json.loads(MANIFEST.read_text())
    if value.get("schema") != SCHEMA:
        raise ValueError("unknown sampled performance manifest")
    ids = [case["id"] for case in value["cases"]]
    if len(set(ids)) != len(ids):
        raise ValueError("duplicate sampled performance case id")
    for case in value["cases"]:
        if case["family"] not in ADAPTERS or case["fixture"] not in value["fixtures"]:
            raise ValueError("case names an unknown family or fixture")
        if type(case["captures"]) is not int or not 1 <= case["captures"] <= 16:
            raise ValueError("a sampled family job requires one to sixteen independent captures")
        training = case["training"]
        if training is not None and (len(training) != 2 or
            any(type(x) is not int or x < 1 for x in training) or training[1] <= training[0]):
            raise ValueError("abbreviation needs two increasing positive work counts")
    return value


@lru_cache(maxsize=None)
def load_source(root, filename):
    """Resolve each stage to the requested source checkout or archive."""
    if Path(filename).name != filename or not filename.endswith(".py"):
        raise ValueError("invalid source module name")
    directory = Path(root).resolve(strict=True) / "tools"
    path = directory / filename
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"missing or symlinked source module {filename}")
    if "-" in filename:
        name = "_triad_family_" + hashlib.sha256(os.fsencode(path)).hexdigest()[:16]
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    else:
        module = importlib.import_module(path.stem)
    if Path(module.__file__).resolve() != path:
        raise ValueError(f"receiver module loaded outside requested source root: {filename}")
    return module


def _guard_imports(root):
    directory = Path(root) / "tools"
    for name, module in tuple(sys.modules.items()):
        if name.startswith("triad_") and not name.startswith("triad_perf"):
            file = getattr(module, "__file__", None)
            if file and Path(file).suffix == ".py" and Path(file).resolve().parent != directory:
                raise ValueError(f"receiver dependency imported from a different source: {name}")


def load_harness_module(name):
    """Bind benchmark-owned code to the current harness despite the source path."""
    path = WORKER.with_name(name + ".py")
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"missing or symlinked benchmark module {name}")
    module = sys.modules.get(name)
    if module is None:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    if Path(module.__file__).resolve() != path:
        raise ValueError(f"benchmark module loaded outside current harness: {name}")
    return module


def stage_sources(root, case):
    """Hash exact adapter and receiver dependencies, independent of unrelated DSP."""
    paths = [Path(root) / "tools" / name for name in case["dependencies"]]
    adapter = WORKER.with_name(ADAPTERS[case["family"]] + ".py")
    return {str(p.name): file_sha(p) for p in (*paths, adapter)}


def validate_measurement(case, value, limit):
    if not isinstance(value, dict) or set(value) != {"seconds", "work", "regime", "result"}:
        raise ValueError("family adapter returned malformed measurement")
    if not value["seconds"] or any(not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0
                                   for v in value["seconds"].values()):
        raise ValueError("invalid stage timing")
    work = value["work"]
    count, target = work["count"], work.get("target_count")
    if (work["unit"] != case["unit"] or type(count) is not int or type(target) is not int
            or not 1 <= count <= target or count != (target if limit is None else limit)):
        raise ValueError("measured work count does not match the requested operation")
    if not value["regime"] or not value["result"]:
        raise ValueError("missing work regime or numerical results")
    # Reject NumPy/Torch scalars or nonfinite values at the protocol boundary.
    digest(value)
    return value


def triton_policy(case, device):
    if case["family"] not in TRITON_PHASE or not device.startswith("cuda"):
        return None
    cache = os.environ.get("TRITON_CACHE_DIR")
    return {
        "distribution_version": importlib.metadata.version("triton"),
        "disk_cache": "inherited backend cache; harness does not clear, seed or measure cache hits",
        "explicit_cache_directory_sha256": hashlib.sha256(os.fsencode(cache)).hexdigest() if cache else None,
        "always_compile_environment": os.environ.get("TRITON_ALWAYS_COMPILE"),
        "process": "one persistent interpreter per source; runtime/compiler work can warm during the discarded invocation",
        "receiver_state": "new state each score; graph capture and replay follow each invocation's actual block intervals",
    }


def resolve_worker_device(torch, requested):
    if requested == "cpu":
        return requested
    device = torch.device(requested)
    if device.type != "cuda" or not torch.cuda.is_available():
        raise ValueError(f"unsupported or unavailable receiver device {requested}")
    if device.index is None:
        device = torch.device("cuda", torch.cuda.current_device())
    torch.cuda.set_device(device)
    return str(device)


def serve(root, threads):
    this = sys.modules[__name__]
    if sys.modules.setdefault("triad_perf_families", this) is not this:
        raise ValueError("worker has a conflicting sampled performance harness")
    root = Path(root).resolve(strict=True)
    import numpy
    import scipy
    import torch
    sys.path.insert(0, str(root / "tools"))
    torch.set_num_threads(threads)
    torch.set_num_interop_threads(1)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    prepared = {}
    for line in sys.stdin:
        request = json.loads(line)
        action = request.get("action")
        try:
            if action == "hello":
                result = {"root": str(root), "python": sys.version.split()[0],
                          "numpy": numpy.__version__, "scipy": scipy.__version__,
                          "torch": torch.__version__, "threads": torch.get_num_threads(),
                          "cuda_available": torch.cuda.is_available(), "cuda_runtime": torch.version.cuda,
                          "tf32_allowed": False}
            elif action == "close":
                result = {"closed": True}
            elif action == "family-prepare":
                case, requested = request["case"], request["device"]
                start = time.perf_counter()
                module = load_harness_module(ADAPTERS[case["family"]])
                for dependency in case["dependencies"]:
                    load_source(root, dependency)
                device = resolve_worker_device(torch, requested)
                if device.startswith("cuda"):
                    torch.cuda.synchronize(device)
                hardware = load_harness_module("triad_perf_hardware")
                _guard_imports(root)
                sources = stage_sources(root, case)
                environment = hardware.discover_hardware(device, threads)
                environment["triton_jit_policy"] = triton_policy(case, device)
                prepared[case["id"]] = (module, sources, environment, requested, device)
                result = {"source_sha256": sources, "prepare_seconds": time.perf_counter()-start,
                          "environment": environment, "resolved_device": device}
            elif action == "family-run":
                case = request["case"]
                module, sources, _, requested, device = prepared[case["id"]]
                files = tuple(Path(p) for p in request["fixtures"])
                fixture = files[0] if len(files) == 1 else files
                limit = request["limit"]
                if request["device"] != requested:
                    raise ValueError("receiver device differs from the prepared environment")
                start = time.perf_counter()
                if device.startswith("cuda"):
                    torch.cuda.synchronize(device)
                value = module.run_case(root, fixture, case, device, request["precision"], limit)
                if device.startswith("cuda"):
                    torch.cuda.synchronize(device)
                elapsed = time.perf_counter()-start
                _guard_imports(root)
                result = {"receiver_wall_seconds": elapsed, "receiver": validate_measurement(case, value, limit),
                          "source_sha256": sources, "complete_receiver": limit is None}
            elif action == "family-verify":
                case = request["case"]
                _, expected, environment, _, device = prepared[case["id"]]
                if stage_sources(root, case) != expected:
                    raise RuntimeError("receiver stage source changed during timing")
                result = {"source_sha256": expected}
                if request.get("verify_environment"):
                    hardware = load_harness_module("triad_perf_hardware")
                    actual = hardware.discover_hardware(device, threads)
                    actual["triton_jit_policy"] = triton_policy(case, device)
                    result["environment_matches_setup"] = digest(actual) == digest(environment)
            else:
                raise ValueError("unknown family worker action")
            envelope = {"ok": True, "result": result}
        except Exception as error:
            envelope = {"ok": False, "error": f"{type(error).__name__}: {error}"}
        print(json.dumps(envelope, allow_nan=False, separators=(",", ":")), flush=True)
        if action == "close":
            break


def native_arguments(binary, profile, seed, threads, output):
    return [str(binary), "--mode", profile["mode"], *profile["args"],
            "--seed", str(seed), "--signal-bit", str(seed % 2),
            "--threads", str(threads), "--output", str(output)]


def remaining(deadline):
    left = deadline - time.monotonic()
    if left <= 0:
        raise TimeoutError("sampled-family measurement deadline reached")
    return left


def export_fixtures(binary, profile, seed, threads, directory, count, deadline):
    """Measure fresh native processes, complete sampled tails, and export writes."""
    commands, paths, times = [], [], []
    for i in range(count):
        out = directory / f"capture-{i}"
        argv = native_arguments(binary, profile, seed + i, threads, out)
        start = time.perf_counter()
        completed = subprocess.run(argv, capture_output=True, text=True, timeout=remaining(deadline))
        elapsed = time.perf_counter()-start
        if completed.returncode:
            raise RuntimeError(f"native sampled export failed: {completed.stderr.strip()}")
        if not (out / "manifest.json").is_file():
            raise RuntimeError("native export returned without a finalized manifest")
        paths.append(out)
        commands.append(argv)
        times.append(elapsed)
    return paths, {"commands": commands, "invocation_wall_seconds": times, "wall_seconds": sum(times),
                   "seed": seed, "count": count}


def fixture_evidence(paths):
    entries = []
    for path in paths:
        start = time.perf_counter()
        files = sorted(path.iterdir())
        if any(p.is_symlink() or not p.is_file() or p.suffix in (".partial", ".tmp") for p in files):
            raise ValueError("sampled fixture contains unfinished or unsafe files")
        identity = {p.name: file_sha(p) for p in files}
        metadata = json.loads((path / "manifest.json").read_text())
        entries.append({"sha256": identity, "bytes": sum(p.stat().st_size for p in files),
                        "sample_count": metadata["sample_count"], "block_count": metadata["block_count"],
                        "hashing_wall_seconds": time.perf_counter()-start})
    return entries


def receive(worker, paths, case, device, precision, limit=None):
    start = time.perf_counter()
    result = worker.request({"action": "family-run", "case": case, "device": device,
                             "precision": precision, "limit": limit,
                             "fixtures": list(map(str, paths))})
    result["request_wall_seconds"] = time.perf_counter()-start
    return result


def measure_job(worker, binary, profile, case, seed, native_threads, directory,
                device, precision, deadline, *, limits=(None,)):
    """Fresh sampled simulation plus complete receiver, or paired prefixes."""
    start = time.perf_counter()
    paths, generation = export_fixtures(binary, profile, seed, native_threads, directory,
                                         case.get("captures", 1), deadline)
    receipts = [receive(worker, paths, case, device, precision, limit) for limit in limits]
    wall = time.perf_counter()-start
    return {"generation": generation, "receiver_runs": receipts,
            "complete_pipeline_wall_seconds" if limits == (None,) else "training_pipeline_wall_seconds": wall,
            "fixture": fixture_evidence(paths)}


def complete_work_signature(row):
    receipt, = row["receiver_runs"]
    receiver = receipt["receiver"]
    work = receiver["work"]
    if not receipt["complete_receiver"] or work.get("count") != work.get("target_count") or type(work.get("target_count")) is not int:
        raise ValueError("full endpoint did not execute its public target work")
    return {"unit": work["unit"], "target_count": work["target_count"],
            "regime_sha256": digest(receiver["regime"])}


def affine_prediction(rows, counts, target):
    """Fit request-wall costs within one exact measured compute regime.

    Fit each outer observation separately. Outer observations pair the two
    work counts on one sampled export; only complete independent fixtures
    become additional observations. A large negative intercept signals that
    the two costs do not support extrapolation.
    """
    if not rows or len(counts) != 2:
        raise ValueError("two timing counts and at least one outer observation required")
    small, large = counts
    if type(target) is not int or not 1 <= small < large < target <= 8*large:
        raise ValueError("sampled-family extrapolation exceeds the declared work range")
    fingerprints, units = set(), set()
    generations, receivers, predictions = [], [], []
    for row in rows:
        first, second = row["receiver_runs"]
        for sample, expected in zip((first, second), counts):
            if sample["receiver"]["work"]["count"] != expected or sample["complete_receiver"]:
                raise ValueError("training did not execute the two abbreviated work counts")
            if sample["receiver"]["work"]["target_count"] != target:
                raise ValueError("training captures disagree on the target work count")
            fingerprints.add(digest(sample["receiver"]["regime"]))
            units.add(sample["receiver"]["work"]["unit"])
        generation = row["generation"]["wall_seconds"]
        a, b = (r["request_wall_seconds"] for r in (first, second))
        slope = (b-a)/(large-small)
        fixed = a-slope*small
        prediction = fixed+slope*target
        if slope <= 0 or fixed < -0.1*b or prediction <= 0:
            raise ValueError("unstable or nonpositive receiver cost fit")
        generations.append(generation)
        receivers.append(prediction)
        predictions.append(generation + prediction)
    if len(fingerprints) != 1 or len(units) != 1:
        raise ValueError("training changed the state, FFT, tiling or memory regime")
    return {"training_counts": list(counts), "target_count": target,
            "regime_sha256": fingerprints.pop(),
            "unit": units.pop(),
            "generation_seconds_point": statistics.geometric_mean(generations),
            "receiver_request_seconds_point": statistics.geometric_mean(receivers),
            "complete_pipeline_seconds_point": statistics.geometric_mean(predictions),
            "complete_pipeline_arithmetic_mean_seconds": statistics.fmean(predictions),
            "outer_pipeline_predictions_seconds": predictions,
            "generation_scope": "direct fresh full sampled export at each training observation",
            "receiver_scope": "affine request-wall estimate with fixed receiver dimensions",
            "aggregation": "geometric mean of paired generation and extrapolated receiver seconds"}


def validation_row(row, forecast):
    observation, = row["receiver_runs"]
    signature = complete_work_signature(row)
    if signature != {name: forecast[name] for name in signature}:
        raise ValueError("full validation changed the trained compute regime")
    stages = {
        "pipeline": (row["complete_pipeline_wall_seconds"], forecast["complete_pipeline_seconds_point"]),
        "receiver": (observation["request_wall_seconds"], forecast["receiver_request_seconds_point"]),
        "generation": (row["generation"]["wall_seconds"], forecast["generation_seconds_point"]),
    }
    result = dict(row)
    for stage, (actual, predicted) in stages.items():
        result[f"{stage}_relative_error"] = actual/predicted-1
        result[f"{stage}_log_error"] = math.log(actual/predicted)
    return result


def case_worker_setup(worker, case, device):
    start = time.perf_counter()
    setup = worker.request({"action": "family-prepare", "case": case, "device": device})
    return {**setup, "request_wall_seconds": time.perf_counter()-start}


def qualify_case(worker, binary, profile, case, output, *, device, precision, native_threads, deadline,
                 training_repeats=3, mode="qualify", maximum_holds=9, sustained_seconds=0.):
    """Validate a frozen full-pipeline prediction on disjoint physical seeds."""
    from triad_perf_adaptive import model_confirmation_policy, qualify_log_residuals

    output.mkdir(parents=True, exist_ok=False)
    setup = case_worker_setup(worker, case, device)
    state = {"case": case, "fixture_profile": profile, "setup": setup,
             "precision": precision, "device": device, "status": "running",
             "schema": SCHEMA, "mode": mode, "source_binary_sha256": file_sha(binary),
             "measurement_driver_sha256": file_sha(WORKER), "worker_environment": worker.identity,
             "measurement_dependencies_sha256": {name: file_sha(WORKER.with_name(name))
                                                  for name in MEASUREMENT_DEPENDENCIES},
             "native_threads": native_threads,
             "worker_startup_seconds": worker.external_startup_seconds,
             "timing_scope": "fresh native sampled export through complete result IPC in a persistent worker; startup/module setup separately observed",
             "evidence_scope": "input hashing and evidence JSON writes outside timed target",
             "training": [], "holdouts": [], "confirmation": []}
    base = int(case["seed"])
    with tempfile.TemporaryDirectory(prefix="captures-", dir=output) as scratch:
        scratch = Path(scratch)
        counts = case["training"]
        # One complete warmup only for direct measurements. For modeled cases
        # the first prefix pair is the discarded startup/dispatch warmup.
        if mode != "direct" and counts is not None:
            run_limits = tuple(counts)
        else:
            run_limits = (None,)
        state["discarded_warmup"] = measure_job(
            worker, binary, profile, case, base, native_threads, scratch/"warmup",
            device, precision, deadline, limits=run_limits)
        if mode == "direct" or counts is None:
            expected_work = complete_work_signature(state["discarded_warmup"])
            for i in range(training_repeats):
                row = measure_job(
                    worker, binary, profile, case, base+100000+16*i, native_threads,
                    scratch/f"direct-{i}", device, precision, deadline)
                state["holdouts"].append(row)
                if complete_work_signature(row) != expected_work:
                    state["status"] = "work_regime_changed"
                    break
            else:
                state["status"] = "direct_full"
        else:
            for i in range(training_repeats):
                order = tuple(counts if i % 2 == 0 else reversed(counts))
                row = measure_job(worker, binary, profile, case, base+16*(i+1), native_threads,
                                  scratch/f"train-{i}", device, precision, deadline, limits=order)
                if i % 2:
                    row["receiver_runs"].reverse()
                state["training"].append(row)
            target = state["training"][0]["receiver_runs"][0]["receiver"]["work"]["target_count"]
            try:
                forecast = affine_prediction(state["training"], counts, target)
            except ValueError as error:
                state["status"] = "unqualified"
                state["error"] = str(error)
                write_json(output/"result.json", state)
                return state
            policy = model_confirmation_policy()
            state["forecast"] = forecast
            expected_work = {name: forecast[name] for name in ("unit", "target_count", "regime_sha256")}
            state["confirmation_policy"] = policy.to_dict()
            state["commitment_sha256"] = digest(state)
            write_json(output/"forecast.json", state)
            state["status"] = "unqualified"
            for i in range(maximum_holds):
                if deadline-time.monotonic() < 1.2*forecast["complete_pipeline_seconds_point"]:
                    state["confirmation_stop_reason"] = "deadline"
                    break
                try:
                    observation = measure_job(worker, binary, profile, case, base+100000+16*i,
                                              native_threads, scratch/f"hold-{i}", device, precision, deadline)
                except (ValueError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
                    state["confirmation_stop_reason"] = "full_measurement_failed"
                    state["confirmation_error"] = f"{type(error).__name__}: {error}"
                    break
                try:
                    state["holdouts"].append(validation_row(observation, forecast))
                except ValueError as error:
                    state["holdouts"].append({**observation, "model_validation_error": str(error)})
                    state["confirmation_stop_reason"] = "work_regime_changed"
                    break
                if i+1 in policy.looks:
                    checks = {stage: qualify_log_residuals(
                        [r[f"{stage}_log_error"] for r in state["holdouts"]],
                        maximum_relative_error=.05, confidence=policy.confidence)
                        for stage in ("pipeline", "receiver", "generation")}
                    state["confirmation"].append(checks)
                    outcomes = {check["status"] for check in checks.values()}
                    if outcomes == {"validated"}:
                        state["status"] = "validated"
                        state["confirmation_stop_reason"] = "validated"
                        break
                    if "invalid" in outcomes:
                        state["confirmation_stop_reason"] = "at_least_one_stage_mismatch"
                        break
        if sustained_seconds and state["status"] in ("validated", "direct_full"):
            point = (state["forecast"]["complete_pipeline_arithmetic_mean_seconds"]
                     if state["status"] == "validated" else
                     statistics.fmean(row["complete_pipeline_wall_seconds"] for row in state["holdouts"]))
            count = max(2, math.ceil(sustained_seconds/point))
            if 1.2*count*point < remaining(deadline):
                state["campaign"] = sustain(
                    worker, binary, profile, case, base+200000, native_threads, scratch,
                    device, precision, deadline, count, point, output, expected_work)
            else:
                state["sustained_status"] = "insufficient_declared_budget"
    worker.request({"action": "family-verify", "case": case})
    if file_sha(binary) != state["source_binary_sha256"]:
        raise ValueError("native sampled exporter changed during qualification")
    if any(file_sha(WORKER.with_name(name)) != hash_
           for name, hash_ in state["measurement_dependencies_sha256"].items()):
        raise ValueError("measurement source changed during qualification")
    write_json(output/"result.json", state)
    return state


def sustain(worker, binary, profile, case, seed, native_threads, directory,
            device, precision, deadline, count, point, output, expected_work):
    """One predetermined campaign through the shared, already initialized worker."""
    forecast = {"count": count, "warm_pipeline_predicted_seconds": count*point,
                "shared_startup_seconds": worker.external_startup_seconds,
                "expected_complete_work": expected_work,
                "scope": "serial independent sampled exports and receiver jobs; worker setup paid once"}
    write_json(output/"campaign-forecast.json", forecast)
    # Preserve uninterrupted complete processing. Compute the fixture hashes
    # after the endpoint so they cannot interrupt thermal/cache validation.
    capture_paths, rows = [], []
    start = time.perf_counter()
    for i in range(count):
        row_start = time.perf_counter()
        paths, generation = export_fixtures(binary, profile, seed+16*i, native_threads,
                                             directory/f"sustain-{i}", case.get("captures", 1), deadline)
        receiver = receive(worker, paths, case, device, precision)
        rows.append({"generation": generation, "receiver_runs": [receiver],
                     "complete_pipeline_wall_seconds": time.perf_counter()-row_start})
        capture_paths.append(paths)
    wall = time.perf_counter()-start
    changed = []
    for i, (row, paths) in enumerate(zip(rows, capture_paths, strict=True)):
        row["fixture"] = fixture_evidence(paths)
        if complete_work_signature(row) != expected_work:
            changed.append(i)
    quarter = max(1, count//4)
    first = statistics.fmean(row["complete_pipeline_wall_seconds"] for row in rows[:quarter])
    last = statistics.fmean(row["complete_pipeline_wall_seconds"] for row in rows[-quarter:])
    return {**forecast, "warm_pipeline_actual_seconds": wall,
            "relative_error": wall/(count*point)-1, "last_over_first_quarter": last/first,
            "work_regime_matched": not changed, "changed_job_indices": changed,
            "jobs": rows}


def cli(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serve", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--mode", choices=("direct", "qualify"), default="qualify")
    parser.add_argument("--budget-seconds", type=float, default=600.)
    parser.add_argument("--training-repeats", type=int, default=3)
    parser.add_argument("--maximum-holds", type=int, choices=(3, 5, 9, 17), default=9)
    parser.add_argument("--sustained-seconds", type=float, default=0.)
    args = parser.parse_args(argv)
    if not 1 <= args.threads <= 256 or not 1 <= args.native_threads <= 256:
        parser.error("thread counts must be in [1,256]")
    if args.serve:
        serve(args.source_root, args.threads)
        return 0
    if not args.binary or not args.output or not args.cases:
        parser.error("--binary, --output and at least one --case are required")
    if not 3 <= args.training_repeats <= 33 or not 0 < args.budget_seconds <= 28800 or not 0 <= args.sustained_seconds <= 3600:
        parser.error("invalid repeat count or budget")
    config = manifest()
    by_id = {case["id"]: case for case in config["cases"]}
    if set(args.cases)-by_id.keys() or len(set(args.cases)) != len(args.cases):
        parser.error("unknown or repeated case")
    binary = args.binary.resolve(strict=True)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    # Reuse the main suite's process protocol and hard per-request deadline.
    common = runpy.run_path(str(WORKER.with_name("triad-performance.py")))
    deadline = time.monotonic()+args.budget_seconds
    started = time.perf_counter()
    worker = common["ReceiverWorker"](args.source_root, threads=args.threads, deadline=deadline,
                                       log_directory=out, name="families", adapter=WORKER)
    status = {}
    try:
        for name in args.cases:
            case = by_id[name]
            if args.device not in case["devices"] or (args.device == "cuda" and not worker.identity["cuda_available"]):
                status[name] = "unsupported_backend"
                continue
            try:
                row = qualify_case(worker, binary, config["fixtures"][case["fixture"]], case, out/name,
                                   device=args.device, precision=args.precision, native_threads=args.native_threads,
                                   deadline=deadline, training_repeats=args.training_repeats, mode=args.mode,
                                   maximum_holds=args.maximum_holds, sustained_seconds=args.sustained_seconds)
                status[name] = row["status"]
            except (ValueError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
                status[name] = f"error: {type(error).__name__}: {error}"
    finally:
        worker.close()
        write_json(out/"summary.json", {"schema": SCHEMA, "case_status": status,
            "wall_seconds": time.perf_counter()-started, "complete_scope_qualification": False})
    return 0 if all(s in ("validated", "direct_full") for s in status.values()) else 2


if __name__ == "__main__":
    raise SystemExit(cli())
