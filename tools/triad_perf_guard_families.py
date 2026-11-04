#!/usr/bin/env python3
"""Full sampled-family targets for the inexpensive, reject-only guard.

Routine screening directly measures fresh native generation and the complete
receiver pipeline. No composed-stage forecast is needed for its full-runtime
measurement. Explicit qualification diagnostics may instead share captures and
report a composed estimate; those estimates are not direct pipeline timings.
Every receiver call still loads and prepares its full workspace.

Workers retain imports/device initialization, not prepared receiver state. Their
startup, family setup, shared exports, hashing and cleanup are charged once in
the campaign ledger; they are not silently added to every family target.
"""
from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import tempfile
import time

import triad_perf_families as sample
from triad_perf_family_regression import numerical_agreement
from triad_perf_lifecycle import group_plan
from triad_perf_staged import _capture_signature


SOURCES = ("reference", "candidate")
BOUNDARY = (
    "warm source-isolated worker; fresh native generation/channel/export, complete "
    "sampled load and receiver preparation, device transfers, full receiver and "
    "final result IPC; excludes worker import/device setup, fixture hashing and "
    "evidence serialization, which are charged separately to campaign wall time"
)
HARNESS_FILES = (
    "triad_perf_guard_families.py", "triad_perf_families.py",
    "triad_perf_family_regression.py", "triad_perf_lifecycle.py",
    "triad_perf_staged.py", "triad-performance.py", "triad_perf_hardware.py",
    "triad_perf_adaptive.py",
    "triad-performance-families.json",
)
PRIVATE_STATE_ALLOCATION_FIELDS = ("owned_state_bytes", "working_bytes_bound")
ALLOCATION_COMPARISON_CONTRACT = (
    "only state-family receiver.regime.resolved.owned_state_bytes and "
    "receiver.regime.resolved.working_bytes_bound may differ across revisions; "
    "matching presence and nonnegative integer values required; raw per-source "
    "regimes remain frozen across observations; all other work/regime fields retained"
)
LIBRARY_ENUMERATION_CONTRACT = (
    "receiver.regime.threads.library_pools is an unordered library enumeration; "
    "canonical comparison preserves every entry, duplicate, path, version and "
    "thread-control field; original order retained in raw evidence"
)


def _stamp():
    return {"utc": datetime.now(timezone.utc).isoformat(),
            "monotonic_seconds": time.monotonic()}


def _positive_seconds(value, label):
    if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label} must be finite and positive")
    return float(value)


def canonical_library_pair(pair):
    """Canonicalize inspection order, preserving the full library multiset."""
    result = deepcopy(pair)
    for name in SOURCES:
        for receipt in result[name]["receiver_runs"]:
            threads = receipt["receiver"]["regime"].get("threads", {})
            if "library_pools" in threads:
                pools = threads["library_pools"]
                if not isinstance(pools, list) or any(not isinstance(row, dict) for row in pools):
                    raise ValueError("thread library inspection must be a list of metadata mappings")
                # Hashing canonical JSON supplies a stable order without
                # dropping fields or deduplicating distinct loaded libraries.
                threads["library_pools"] = sorted(pools, key=sample.digest)
    return result


def comparison_pair(pair, case):
    """Ignore two private allocation estimates only for cross-revision parity.

    These state-adapter diagnostics describe owned storage or conservative
    working-storage estimates, not public state dimensions or executed work.
    A full direct comparison may legitimately improve them without changing the
    operation. The memory budget, backend, dtype, tiles and all algorithm fields
    remain strict. This is not a compatibility rule for extrapolation or for
    another family. Existing historical qualification helpers remain unchanged.
    """
    result = canonical_library_pair(pair)
    ignored = {name: [] for name in SOURCES}
    if case["family"] not in sample.STATE:
        return result, ignored
    for name in SOURCES:
        for receipt in result[name]["receiver_runs"]:
            resolved = receipt["receiver"]["regime"].get("resolved", {})
            removed = {}
            for field in PRIVATE_STATE_ALLOCATION_FIELDS:
                if field in resolved:
                    value = resolved.pop(field)
                    if type(value) is not int or value < 0:
                        raise ValueError(f"private state allocation {field} must be a nonnegative integer")
                    removed[field] = value
            ignored[name].append(removed)
    if ([set(row) for row in ignored["reference"]] !=
            [set(row) for row in ignored["candidate"]]):
        raise ValueError("paired private state allocation diagnostics have different presence")
    return result, ignored


class FamilyWorkloads:
    """Lazy adapter: constructing/describing it performs no workload execution."""

    def __init__(self, context):
        self.context = dict(context)
        self.roots = {"reference": Path(context["reference_root"]).resolve(),
                      "candidate": Path(context["candidate_root"]).resolve()}
        self.binaries = {"reference": Path(context["reference_binary"]).resolve(),
                         "candidate": Path(context["binary"]).resolve()}
        self.device = context.get("device", "cpu")
        if self.device != "cpu" and not (isinstance(self.device, str) and
                                         (self.device == "cuda" or self.device.startswith("cuda:"))):
            raise ValueError("family device must be cpu or cuda[:index]")
        self.precision = context.get("precision", "fp32")
        if self.precision not in ("fp32", "fp64"):
            raise ValueError("family precision must be fp32 or fp64")
        self.screen_method = context.get("family_screen_method", "direct-full")
        if self.screen_method not in ("direct-full", "shared-composed-diagnostic"):
            raise ValueError("family_screen_method must be direct-full or shared-composed-diagnostic")
        self.threads = context.get("threads", 1)
        self.native_threads = context.get("native_threads", self.threads)
        for label in ("threads", "native_threads"):
            if type(getattr(self, label)) is not int or getattr(self, label) < 1:
                raise ValueError(f"{label} must be a positive integer")
        self.deadline = context["deadline"]
        if type(self.deadline) not in (int, float) or not math.isfinite(self.deadline):
            raise ValueError("deadline must be a finite absolute monotonic timestamp")
        self.output = Path(context["output"]) / "sampled-families"
        self.manifest = deepcopy(sample.manifest())
        backend = "cuda" if self.device.startswith("cuda") else "cpu"
        self.cases = {"family:" + case["id"]: case for case in self.manifest["cases"]
                      if backend in case["devices"]}
        known = {case["id"]: case for case in self.cases.values()}
        self.group_counts = group_plan(list(known), known)
        self.identity = self._identity()
        self.workers, self.prepared, self.groups = {}, set(), {}
        self._worker_context_sequence = 0
        self._worker_context = None
        self._warm_seed_cursor = 2**63 - 1
        self.run_ids, self.generation_seeds = set(), set()
        self.confirmation_seeds, self.screen_profile_seeds = set(), {}
        self.signatures = {}
        self.scratch = None
        self.closed = False
        self.ledger = {
            "boundary": BOUNDARY, "started": None, "finished": None,
            "worker_setup": {}, "worker_contexts": [], "family_setup": {}, "shared_generation": {},
            "measurements": [], "operations": [], "generation_seed_claims": [],
            "identity_before": self.identity,
            "receiver_cache": "imports and device initialization only; full state rebuilt each call",
            "screen_method": self.screen_method,
            "screen_reuse": ("worker imports/device initialization only; each screen generates its complete native capture"
                             if self.screen_method == "direct-full" else
                             "qualification diagnostic: exact native profile; captures reused within this adapter invocation only"),
            "unavailable_cases": [case["id"] for case in self.manifest["cases"]
                                  if backend not in case["devices"] or self._unavailable_reason(case)],
        }

    def _unavailable_reason(self, case):
        if case["family"] == "phase-tensor" and self.precision != "fp32":
            return "phase-tensor requires explicit FP32 bulk state; CUDA FP64 tensor execution is unsupported"
        return None

    def _identity(self):
        return {
            "roots": {name: str(root) for name, root in self.roots.items()},
            "native_binary_sha256": {name: sample.file_sha(path) for name, path in self.binaries.items()},
            "receiver_source_sha256": {
                name: {key: sample.stage_sources(root, case) for key, case in self.cases.items()}
                for name, root in self.roots.items()},
            "harness_sha256": {name: sample.file_sha(Path(__file__).with_name(name))
                               for name in HARNESS_FILES},
            "manifest_sha256": sample.digest(self.manifest),
            "screen_method": self.screen_method,
        }

    def _check_identity(self):
        try:
            current = self._identity()
        except OSError as exc:
            raise ValueError("family source, harness or native binary became inaccessible") from exc
        if current != self.identity:
            raise ValueError("family source, harness or native binary changed during the guard")
        return current

    def descriptors(self):
        return [{"id": key, "kind": "sampled-family", "family": case["family"],
                 "boundary": BOUNDARY, "full_geometry": deepcopy(case),
                 "generation_profile": deepcopy(self.manifest["fixtures"][case["fixture"]]),
                 "precision": self.precision, "device": self.device,
                 "available": self._unavailable_reason(case) is None,
                 "reason": self._unavailable_reason(case),
                 "screen_method": self.screen_method,
                 "screen_timing_method": ("direct_full" if self.screen_method == "direct-full"
                                          else "shared_composed_diagnostic"),
                 "screen_prediction_model_used": self.screen_method != "direct-full",
                 "confirmation_method": "fresh complete measure_job(limits=(None,)); no extrapolation",
                 "cross_revision_allocation_contract": ALLOCATION_COMPARISON_CONTRACT,
                 "library_enumeration_contract": LIBRARY_ENUMERATION_CONTRACT,
                 "source_identity_sha256": sample.digest(self.identity)}
                for key, case in self.cases.items()]

    def _ensure_workers(self, order=SOURCES, *, confirmation=False):
        if self.closed:
            raise ValueError("family adapter is closed")
        sample.remaining(self.deadline)
        if self.ledger["started"] is None:
            self.ledger["started"] = _stamp()
            self.output.mkdir(parents=True, exist_ok=False)
            self.scratch = tempfile.TemporaryDirectory(prefix="fixtures-", dir=self.output)
        api = sample.load_source(Path(__file__).resolve().parents[1], "triad-performance.py")
        if self.workers:
            return
        self._worker_context_sequence += 1
        context_id = f"family-context-{self._worker_context_sequence:04d}"
        context_started = time.perf_counter()
        context = {"id": context_id, "creation_order": list(order), "confirmation": confirmation,
                   "workers": {}, "family_setup": {}}
        log_directory = self.output / "workers" / context_id
        log_directory.mkdir(parents=True, exist_ok=False)
        for name in order:
            if name in self.workers:
                continue
            start = time.perf_counter()
            worker = api.ReceiverWorker(self.roots[name], threads=self.threads,
                                       deadline=self.deadline, log_directory=log_directory,
                                       name=name, adapter=sample.WORKER)
            self.workers[name] = worker
            worker_setup = {
                "source": name, "context_id": context_id,
                "pid": getattr(getattr(worker, "process", None), "pid", None),
                "worker_instance": id(worker), "log_directory": str(log_directory),
                "wall_seconds": time.perf_counter() - start,
                "external_startup_seconds": worker.external_startup_seconds,
                "identity": worker.identity,
            }
            context["workers"][name] = worker_setup
        context["setup_wall_seconds"] = time.perf_counter() - context_started
        self._worker_context = context
        self.ledger["worker_setup"][context_id] = deepcopy(context["workers"])
        self.ledger["worker_contexts"].append({"id": context_id,
            "creation_order": list(order), "confirmation": confirmation,
            "setup_wall_seconds": context["setup_wall_seconds"],
            "workers": deepcopy(context["workers"])})

    def _close_worker_context(self, reason):
        context = self._worker_context
        if not self.workers:
            self._worker_context = None
            return {"reason": reason, "workers": {}, "teardown_wall_seconds": 0.}
        started = time.perf_counter()
        records, failures = {}, []
        for name, worker in list(self.workers.items()):
            one = time.perf_counter()
            process = getattr(worker, "process", None)
            close_ack = False
            close_error = None
            close_timeout = False
            pre_cleanup_exit = process.poll() if process is not None else None
            if process is not None and pre_cleanup_exit is None:
                try:
                    response = worker.request({"action": "close"})
                    close_ack = response.get("closed") is True
                    if not close_ack:
                        raise RuntimeError("worker close protocol did not acknowledge closure")
                    process.stdin.close()
                    process.wait(timeout=2.)
                except Exception as exc:
                    close_error = f"{type(exc).__name__}: {exc}"
                    close_timeout = isinstance(exc, (TimeoutError, subprocess.TimeoutExpired))
                    if not close_timeout:
                        failures.append(name + ": close protocol failure: " + close_error)
            # Our close request may already have closed stdin. Suppress the
            # shared facade's second protocol request, while retaining its
            # bounded terminate/kill/reap and stream/thread cleanup finally.
            # This worker is retired permanently; the suite deadline is intact.
            if process is not None:
                worker.deadline = time.monotonic()-1.
            try:
                worker.close()
            except Exception as exc:
                failures.append(name + ": " + str(exc))
            exit_code = process.poll() if process is not None else None
            if process is not None and exit_code is None:
                failures.append(name + ": receiver process remained alive after close")
            records[name] = {"pid": getattr(process, "pid", None),
                             "worker_instance": id(worker),
                             "teardown_wall_seconds": time.perf_counter() - one,
                             "close_protocol_acknowledged": close_ack, "close_protocol_error": close_error,
                             "exit_code": exit_code, "pre_cleanup_exit": pre_cleanup_exit,
                             "cleanup_verified": (process is None or (exit_code is not None and
                                 process.stdin.closed and process.stdout.closed and worker.log.closed and
                                 not worker.reader.is_alive())),
                             "cleanup_mode": ("test_double_unobservable" if process is None else
                                 ("graceful_exit" if close_ack and exit_code == 0 else
                          "owned_forced_after_timeout" if close_timeout else "forced_or_abnormal_exit"))}
            if not records[name]["cleanup_verified"]:
                failures.append(name + ": receiver streams or reader remained alive after close")
            if process is not None and not close_timeout and exit_code not in (0, None):
                failures.append(name + ": worker exited abnormally outside owned timeout cleanup")
        self.workers.clear()
        self.prepared.clear()
        self._worker_context = None
        result = {"id": (context or {}).get("id"), "reason": reason, "workers": records,
                  "teardown_wall_seconds": time.perf_counter() - started,
                  "cleanup_errors": failures}
        self.ledger["worker_contexts"][-1]["teardown"] = deepcopy(result)
        if failures:
            raise RuntimeError("family worker context cleanup failed: " + "; ".join(failures))
        return result

    def _prepare(self, key, case, order):
        for name in order:
            if (name, key) in self.prepared:
                continue
            setup = sample.case_worker_setup(self.workers[name], case, self.device)
            if setup["source_sha256"] != self.identity["receiver_source_sha256"][name][key]:
                raise ValueError("family worker setup source identity differs from frozen source")
            self.ledger["family_setup"].setdefault(key, {})[name] = setup
            if self._worker_context is not None:
                self._worker_context["family_setup"].setdefault(key, {})[name] = deepcopy(setup)
            self.prepared.add((name, key))

    def _verify_worker(self, name, key, case):
        verified = self.workers[name].request({"action": "family-verify", "case": case,
                                               "device": self.device, "verify_environment": True})
        if verified["source_sha256"] != self.identity["receiver_source_sha256"][name][key]:
            raise ValueError("family worker source identity changed")
        if verified.get("environment_matches_setup") is not True:
            raise ValueError("family worker execution environment changed during measurement")
        return verified

    def _claim_seed(self, seed, count, *, phase, profile, screen_case=None):
        if type(seed) is not int or not 0 <= seed < 2**63 - count:
            raise ValueError("family generation seed must be a nonnegative bounded integer")
        seeds = set(range(seed, seed + count))
        profile_hash = sample.digest(profile)
        # Distinct advisory direct screens may deliberately regenerate the
        # same physical seed to compare full receiver families. This is an
        # explicit case namespace, not a fictitious native profile or an IID
        # claim. Confirmations still exclude every prior screen capture seed.
        claim_key = profile_hash if screen_case is None else (profile_hash, screen_case)
        previous = (self.generation_seeds if phase == "confirmation" else
                    self.confirmation_seeds | self.screen_profile_seeds.get(claim_key, set()))
        if seeds & previous:
            raise ValueError("fresh family generation seed overlaps an earlier generation")
        # Reserve before work: interrupted attempts cannot be silently reused.
        # Screens may intentionally pair the same seed across distinct physical
        # profiles; they never enter statistical confirmation. Confirmations
        # remain globally disjoint from every prior capture seed, including any
        # failed generation attempt and any screen profile.
        if phase == "confirmation":
            self.confirmation_seeds.update(seeds)
        else:
            self.screen_profile_seeds.setdefault(claim_key, set()).update(seeds)
        self.generation_seeds.update(seeds)
        self.ledger["generation_seed_claims"].append({
            "phase": phase, "profile_sha256": profile_hash, "screen_case_id": screen_case, "seed": seed,
            "count": count, "capture_seeds": sorted(seeds)})

    def _claim_warmup_seed(self, count, profile):
        """Use a deterministic unused high seed for full-target context warmups."""
        occupied = self.generation_seeds | self.confirmation_seeds
        while self._warm_seed_cursor >= count:
            seed = self._warm_seed_cursor - count
            self._warm_seed_cursor = seed
            values = set(range(seed, seed + count))
            if not values & occupied:
                self._claim_seed(seed, count, phase="confirmation", profile=profile)
                return seed
        raise ValueError("no collision-free family warmup seed remains")

    def _screen_group(self, case, seed, order):
        profile_name = case["fixture"]
        if profile_name in self.groups:
            return self.groups[profile_name], False
        profile = self.manifest["fixtures"][profile_name]
        captures = self.group_counts[profile_name]
        self._claim_seed(seed, captures, phase="screen", profile=profile)
        group = {"seed": seed, "captures": captures, "paths": {}, "raw": {},
                 "profile": deepcopy(profile), "generation_order": list(order)}
        start = time.perf_counter()
        for name in order:
            paths, generation = sample.export_fixtures(
                self.binaries[name], profile, seed, self.native_threads,
                Path(self.scratch.name) / (profile_name + "-" + name), captures, self.deadline)
            group["paths"][name] = paths
            group["raw"][name] = {"generation": generation, "work": _capture_signature(paths),
                                  "fixture": sample.fixture_evidence(paths)}
        if group["raw"]["reference"]["work"] != group["raw"]["candidate"]["work"]:
            raise ValueError("paired full native exports resolved different generation geometry")
        self.groups[profile_name] = group
        self.ledger["shared_generation"][profile_name] = {
            key: value for key, value in group.items() if key != "paths"}
        self.ledger["shared_generation"][profile_name]["setup_wall_seconds"] = time.perf_counter() - start
        return group, True

    def _screen(self, key, case, seed, order):
        group, created = self._screen_group(case, seed, order)
        pair = {}
        count = case["captures"]
        for name in order:
            invocation_started = _stamp()
            receipt = sample.receive(self.workers[name], group["paths"][name][:count],
                                     case, self.device, self.precision, limit=None)
            invocation_finished = _stamp()
            raw = group["raw"][name]
            times = raw["generation"]["invocation_wall_seconds"][:count]
            if len(times) != count:
                raise ValueError("shared generation did not contain the complete capture prefix")
            generation_seconds = sum(_positive_seconds(t, "native invocation time") for t in times)
            pair[name] = {
                "generation": {**raw["generation"], "count": count, "wall_seconds": generation_seconds,
                               "commands": raw["generation"]["commands"][:count],
                               "invocation_wall_seconds": times},
                "fixture": raw["fixture"][:count], "receiver_runs": [receipt],
                "screen_estimated_full_seconds": generation_seconds +
                    _positive_seconds(receipt["request_wall_seconds"], "receiver request time"),
                "receiver_invocation_started": invocation_started,
                "receiver_invocation_finished": invocation_finished,
            }
        return pair, {"method": "shared_composed_diagnostic", "extrapolation": False,
                      "generation_shared": True, "generation_group_created": created,
                      "actual_generation_seed": group["seed"],
                      "requested_seed_used_for_generation": created,
                      "independent_complete_pipeline_observation": False}

    def _direct_screen(self, key, case, seed, order):
        """Measure each complete pipeline once without a timing composition."""
        profile = self.manifest["fixtures"][case["fixture"]]
        self._claim_seed(seed, case["captures"], phase="screen", profile=profile, screen_case=key)
        pair, generation_work = {}, {}
        with tempfile.TemporaryDirectory(prefix="direct-screen-", dir=self.scratch.name) as tmp:
            for name in order:
                directory = Path(tmp) / name
                started = _stamp()
                pair[name] = sample.measure_job(self.workers[name], self.binaries[name],
                    profile, case, seed, self.native_threads, directory, self.device,
                    self.precision, self.deadline, limits=(None,))
                pair[name]["invocation_started"] = started
                pair[name]["invocation_finished"] = _stamp()
                generation_work[name] = _capture_signature(
                    [directory / f"capture-{i}" for i in range(case["captures"])])
        if generation_work["reference"] != generation_work["candidate"]:
            raise ValueError("paired direct screen exports resolved different generation geometry")
        return pair, {"method": "direct_full", "extrapolation": False,
                      "generation_shared": False, "actual_generation_seed": seed,
                      "requested_seed_used_for_generation": True,
                      "direct_complete_pipeline_observation": True,
                      "independent_complete_pipeline_observation": False,
                      "statistical_independence_claim": False,
                      "generation_work": generation_work,
                      "timing_scope": "one actual outer native generation/export and complete receiver request per source; reused worker setup excluded and separately charged"}

    def _confirmation(self, key, case, run_id, seed, order):
        profile = self.manifest["fixtures"][case["fixture"]]
        self._claim_seed(seed, case["captures"], phase="confirmation", profile=profile)
        warm_seed = self._claim_warmup_seed(case["captures"], profile)
        warmups = {}
        with tempfile.TemporaryDirectory(prefix="warmup-", dir=self.scratch.name) as warm_tmp:
            for name in order:
                sample.remaining(self.deadline)
                started = time.perf_counter()
                directory = Path(warm_tmp) / name
                warmups[name] = sample.measure_job(
                    self.workers[name], self.binaries[name], profile, case, warm_seed,
                    self.native_threads, directory, self.device, self.precision,
                    self.deadline, limits=(None,))
                elapsed = time.perf_counter() - started
                sample.validate_measurement(case, warmups[name]["receiver_runs"][0]["receiver"], None)
                context_worker = self._worker_context["workers"][name]
                context_worker["warmup"] = {
                    "wall_seconds": elapsed,
                    "complete_pipeline_wall_seconds": warmups[name]["complete_pipeline_wall_seconds"],
                    "generation_wall_seconds": warmups[name]["generation"]["wall_seconds"],
                    "receiver_request_wall_seconds": warmups[name]["receiver_runs"][0]["request_wall_seconds"],
                    "generation_work": _capture_signature(
                        [directory / f"capture-{i}" for i in range(case["captures"])]),
                    "warmup_seed": warm_seed,
                }
        warm_comparable, _ = comparison_pair(warmups, case)
        warm_signatures = {name: sample.complete_work_signature(warm_comparable[name]) for name in SOURCES}
        if warm_signatures["reference"] != warm_signatures["candidate"]:
            raise ValueError("family full-case warmup work differs across source revisions")
        warm_agreement = numerical_agreement(warm_comparable, self.precision)
        pair, generation_work = {}, {}
        with tempfile.TemporaryDirectory(prefix="fresh-", dir=self.scratch.name) as tmp:
            for name in order:
                directory = Path(tmp) / name
                invocation_started = _stamp()
                pair[name] = sample.measure_job(
                    self.workers[name], self.binaries[name],
                    self.manifest["fixtures"][case["fixture"]], case, seed,
                    self.native_threads, directory, self.device, self.precision,
                    self.deadline, limits=(None,))
                pair[name]["invocation_started"] = invocation_started
                pair[name]["invocation_finished"] = _stamp()
                generation_work[name] = _capture_signature(
                    [directory / f"capture-{i}" for i in range(case["captures"])])
            if generation_work["reference"] != generation_work["candidate"]:
                raise ValueError("paired fresh native exports resolved different generation geometry")
        return pair, {"generation_shared": False, "actual_generation_seed": seed,
                      "generation_work": generation_work,
                      "warmup_seed": warm_seed, "warmup_work_signatures": warm_signatures,
                      "warmup_numerical_agreement": warm_agreement,
                      "independent_complete_pipeline_observation": True,
                      "fixture_cleanup_outside_target_timer": True}

    def measure_pair(self, case_id, run_id, seed, order=SOURCES, phase="screen"):
        start = time.perf_counter()
        outcome = "completed"
        result = None
        try:
            result = self._measure_pair(case_id, run_id, seed, order, phase)
        except Exception as exc:
            outcome = type(exc).__name__
            raise
        finally:
            cleanup_error = None
            if phase in ("confirm", "confirmation"):
                try:
                    teardown = self._close_worker_context("confirmation_period_complete")
                    if result is not None:
                        result["details"]["confirmation_context"]["teardown"] = teardown
                        receipt = result["details"]["confirmation_context"]
                        for source in SOURCES:
                            receipt["isolation_evidence"][source]["teardown"] = \
                                deepcopy(teardown["workers"].get(source))
                        raw_path = Path(result["details"]["raw_receipt"])
                        raw = json.loads(raw_path.read_text(encoding="utf-8"))
                        raw["confirmation_context"] = deepcopy(receipt)
                        raw_path.write_text(json.dumps(raw, sort_keys=True, separators=(",", ":"),
                                                      allow_nan=False) + "\n", encoding="utf-8")
                except Exception as exc:
                    cleanup_error = exc
                    if outcome == "completed":
                        outcome = "WorkerContextCleanupError"
            self.ledger["operations"].append({"run_id": run_id, "case_id": case_id,
                                               "phase": phase, "outcome": outcome,
                                               "active_wall_seconds": time.perf_counter() - start})
            if cleanup_error is not None and outcome == "WorkerContextCleanupError":
                raise cleanup_error
        return result

    def _measure_pair(self, case_id, run_id, seed, order, phase):
        if case_id not in self.cases:
            raise ValueError(f"unknown or unavailable family case: {case_id}")
        unavailable_reason = self._unavailable_reason(self.cases[case_id])
        if unavailable_reason:
            raise ValueError("unavailable family case: " + unavailable_reason)
        if phase == "confirm":
            phase = "confirmation"
        if phase not in ("screen", "confirmation"):
            raise ValueError("family phase must be screen or confirmation")
        if len(order) != 2 or set(order) != set(SOURCES):
            raise ValueError("pair order must contain reference and candidate exactly once")
        if not isinstance(run_id, str) or not run_id.strip() or run_id in self.run_ids:
            raise ValueError("family run_id must be nonempty and unique")
        if type(seed) is not int or not 0 <= seed < 2**63 - self.cases[case_id]["captures"]:
            raise ValueError("family generation seed must be a nonnegative bounded integer")
        self.run_ids.add(run_id)
        started = _stamp()
        before = self._check_identity()
        if phase == "confirmation" and self.workers:
            self._close_worker_context("replace_persistent_screen_workers")
        self._ensure_workers(order, confirmation=(phase == "confirmation"))
        case = self.cases[case_id]
        self._prepare(case_id, case, order)
        raw_path = self.output / ("pair-" + sample.digest(run_id)[:24] + ".json")
        try:
            if phase == "screen":
                measure = self._direct_screen if self.screen_method == "direct-full" else self._screen
                pair, metadata = measure(case_id, case, seed, order)
            else:
                pair, metadata = self._confirmation(case_id, case, run_id, seed, order)
            # Preserve raw full counters, dtypes, timings and scores before validating.
            sample.write_json(raw_path, {"run_id": run_id, "case_id": case_id,
                                         "phase": phase, "seed": seed, "order": list(order),
                                         "started": started, "pair": pair, "metadata": metadata})
            signatures = {}
            for name in SOURCES:
                signatures[name] = sample.complete_work_signature(pair[name])
                for receipt in pair[name]["receiver_runs"]:
                    sample.validate_measurement(case, receipt["receiver"], None)
                    if receipt["source_sha256"] != before["receiver_source_sha256"][name][case_id]:
                        raise ValueError("receiver invocation source differs from frozen source")
            comparable, ignored_allocations = comparison_pair(pair, case)
            comparable_signatures = {name: sample.complete_work_signature(comparable[name])
                                     for name in SOURCES}
            # Freeze each revision's actual regime, including its private
            # allocation values, with only unordered inspection order removed.
            frozen_pair = canonical_library_pair(pair)
            frozen_signatures = {name: sample.complete_work_signature(frozen_pair[name])
                                 for name in SOURCES}
            agreement = numerical_agreement(comparable, self.precision)
            if comparable_signatures["reference"] != comparable_signatures["candidate"]:
                raise ValueError("paired family full work signatures differ")
            if case_id in self.signatures and self.signatures[case_id] != frozen_signatures:
                raise ValueError("family complete work regime changed between guard observations")
            self.signatures[case_id] = frozen_signatures
            verified = {name: self._verify_worker(name, case_id, case) for name in SOURCES}
            after = self._check_identity()
            context = self._worker_context
            confirmation_context = None
            if phase == "confirmation":
                confirmation_context = {
                    "protocol": "fresh-source-context-pair-v1",
                    "identity_kind": "source_isolated_receiver_process",
                    "context_id": context["id"],
                    "creation_order": list(context["creation_order"]),
                    "context_ids": {name: f"{context['id']}:{name}:pid-{row['pid']}"
                                    for name, row in context["workers"].items()},
                    "workers": deepcopy(context["workers"]),
                    "isolation_evidence": {name: {
                        "context_id": f"{context['id']}:{name}:pid-{row['pid']}",
                        "pid": row["pid"], "worker_instance": row["worker_instance"],
                        "setup": {key: row[key] for key in ("wall_seconds", "external_startup_seconds", "log_directory")},
                        "family_setup": deepcopy(context["family_setup"].get(case_id, {}).get(name)),
                        "warmup": deepcopy(row.get("warmup")), "teardown": None}
                        for name, row in context["workers"].items()},
                    "setup_wall_seconds": context["setup_wall_seconds"],
                    "target_boundary_preserved": True,
                    "warmup_policy": "full-target-outside-timer",
                    "warmup_description": "one complete native export and sampled receiver target per worker outside measured target timer",
                    "setup_warmup_costs_charged_to_campaign": True,
                }
            composed = phase == "screen" and self.screen_method == "shared-composed-diagnostic"
            field = "screen_estimated_full_seconds" if composed else "complete_pipeline_wall_seconds"
            result = {
                "run_id": run_id,
                **{name + "_seconds": _positive_seconds(pair[name][field], field) for name in SOURCES},
                "details": {"case_id": case_id, "phase": phase, "boundary": BOUNDARY,
                            "full_fresh": phase == "confirmation" or not composed, "full_work": True,
                            "timing_field": field, "metadata": metadata, "pair": pair,
                            "order": list(order), "requested_seed": seed,
                            "work_signatures": signatures, "numerical_agreement": agreement,
                            "comparison_work_signatures": comparable_signatures,
                            "frozen_work_signatures": frozen_signatures,
                            "cross_revision_private_allocations": ignored_allocations,
                            "cross_revision_allocation_contract": ALLOCATION_COMPARISON_CONTRACT,
                            "library_enumeration_contract": LIBRARY_ENUMERATION_CONTRACT,
                            "source_checks": verified, "identity_before": before,
                            **({"confirmation_context": confirmation_context} if phase == "confirmation" else {}),
                            "identity_after": after, "started": started, "finished": _stamp(),
                            "raw_receipt": str(raw_path)},
            }
            self.ledger["measurements"].append({"run_id": run_id, "case_id": case_id,
                                               "phase": phase, "receipt": str(raw_path),
                                               "screen_method": self.screen_method if phase == "screen" else None,
                                               "timing_field": field,
                                               "source_timing_seconds": {name: pair[name][field] for name in SOURCES},
                                               "generation_invocation_counts": {name: len(pair[name]["generation"]["commands"]) for name in SOURCES},
                                               "started": started, "finished": _stamp(),
                                               "generation_wall_seconds": {
                                                   name: pair[name]["generation"]["wall_seconds"]
                                                   for name in SOURCES}})
            return result
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError("native sampled export exceeded guard deadline") from exc

    def summary(self):
        result = deepcopy(self.ledger)
        actual = sum(group["raw"][name]["generation"]["wall_seconds"]
                     for group in self.groups.values() for name in SOURCES)
        repeated = sum(row["generation_wall_seconds"][name]
                       for row in result["measurements"] if row["phase"] == "screen" and
                       row.get("screen_method") == "shared-composed-diagnostic"
                       for name in SOURCES)
        result["shared_generation_accounting"] = {
            "measured_native_invocation_seconds": actual,
            "estimated_repeated_generation_seconds_for_completed_screens": repeated,
            "estimated_native_invocation_seconds_saved": repeated - actual,
            "scope": "screening only; additive observed invocation costs, not an independent full campaign prediction; a partially visited capture group may have negative savings",
        }
        direct = [row for row in result["measurements"]
                  if row["phase"] == "screen" and row.get("screen_method") == "direct-full"]
        result["direct_screen_accounting"] = {
            "paired_complete_pipeline_observations": len(direct),
            "source_complete_pipeline_observations": 2*len(direct),
            "native_export_invocations": sum(row["generation_invocation_counts"][name]
                                             for row in direct for name in SOURCES),
            "actual_generation_wall_seconds": sum(row["generation_wall_seconds"][name]
                                                  for row in direct for name in SOURCES),
            "actual_complete_pipeline_wall_seconds": sum(row["source_timing_seconds"][name]
                                                         for row in direct for name in SOURCES),
            "prediction_model_used": False,
            "scope": "direct complete target timings, not an extrapolation or statistical independence claim",
        }
        result["adapter_active_wall_seconds"] = (
            sum(row["active_wall_seconds"] for row in result["operations"]) +
            result.get("close_wall_seconds", 0.))
        if result["started"]:
            finish = result["finished"] or _stamp()
            result["adapter_lifetime_wall_seconds"] = (
                finish["monotonic_seconds"] - result["started"]["monotonic_seconds"])
            result["lifetime_scope"] = "includes controller gaps between this adapter's calls; not a sum of workload timers"
        result["closed"] = self.closed
        return result

    def close(self):
        if self.closed:
            return self.summary()
        failures = []
        start = time.perf_counter()
        try:
            self.ledger["identity_after"] = self._check_identity()
            # Shared sampled data must remain unchanged across all consumers.
            for group in self.groups.values():
                for name in SOURCES:
                    current = sample.fixture_evidence(group["paths"][name])
                    expected = group["raw"][name]["fixture"]
                    fields = ("sha256", "bytes", "sample_count", "block_count")
                    if ([{k: v[k] for k in fields} for v in current] !=
                            [{k: v[k] for k in fields} for v in expected]):
                        raise ValueError("shared sampled fixture changed during guard")
        except Exception as exc:
            failures.append(exc)
        finally:
            try:
                self._close_worker_context("adapter_close")
            except Exception as exc:
                failures.append(exc)
            if self.scratch is not None:
                self.scratch.cleanup()
            self.closed = True
            self.ledger["close_wall_seconds"] = time.perf_counter() - start
            self.ledger["finished"] = _stamp()
        if failures:
            self.ledger["close_errors"] = [f"{type(exc).__name__}: {exc}" for exc in failures]
            raise failures[0]
        return self.summary()
