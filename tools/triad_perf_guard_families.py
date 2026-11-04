#!/usr/bin/env python3
"""Full sampled-family targets for the inexpensive, reject-only guard.

Screening shares native captures only within an exact generation profile. Every
receiver call still loads and prepares its full workspace. Such screens are
composed estimates, never independent complete-pipeline observations. A fresh
confirmation starts the native exporter again and measures through completed
receiver result IPC using the established qualification timing boundary.

Workers retain imports/device initialization, not prepared receiver state. Their
startup, family setup, shared exports, hashing and cleanup are charged once in
the campaign ledger; they are not silently added to every family target.
"""
from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone
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
        self.run_ids, self.generation_seeds = set(), set()
        self.confirmation_seeds, self.screen_profile_seeds = set(), {}
        self.signatures = {}
        self.scratch = None
        self.closed = False
        self.ledger = {
            "boundary": BOUNDARY, "started": None, "finished": None,
            "worker_setup": {}, "family_setup": {}, "shared_generation": {},
            "measurements": [], "operations": [], "generation_seed_claims": [],
            "identity_before": self.identity,
            "receiver_cache": "imports and device initialization only; full state rebuilt each call",
            "screen_reuse": "exact native profile; captures reused within this adapter invocation only",
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
                 "screen_method": "shared exact full generation plus fresh complete receiver",
                 "confirmation_method": "fresh complete measure_job(limits=(None,)); no extrapolation",
                 "cross_revision_allocation_contract": ALLOCATION_COMPARISON_CONTRACT,
                 "library_enumeration_contract": LIBRARY_ENUMERATION_CONTRACT,
                 "source_identity_sha256": sample.digest(self.identity)}
                for key, case in self.cases.items()]

    def _ensure_workers(self):
        if self.closed:
            raise ValueError("family adapter is closed")
        sample.remaining(self.deadline)
        if self.ledger["started"] is None:
            self.ledger["started"] = _stamp()
            self.output.mkdir(parents=True, exist_ok=False)
            self.scratch = tempfile.TemporaryDirectory(prefix="fixtures-", dir=self.output)
        api = sample.load_source(Path(__file__).resolve().parents[1], "triad-performance.py")
        for name in SOURCES:
            if name in self.workers:
                continue
            start = time.perf_counter()
            worker = api.ReceiverWorker(self.roots[name], threads=self.threads,
                                       deadline=self.deadline, log_directory=self.output,
                                       name=name, adapter=sample.WORKER)
            self.workers[name] = worker
            self.ledger["worker_setup"][name] = {
                "wall_seconds": time.perf_counter() - start,
                "external_startup_seconds": worker.external_startup_seconds,
                "identity": worker.identity,
            }

    def _prepare(self, key, case, order):
        for name in order:
            if (name, key) in self.prepared:
                continue
            setup = sample.case_worker_setup(self.workers[name], case, self.device)
            if setup["source_sha256"] != self.identity["receiver_source_sha256"][name][key]:
                raise ValueError("family worker setup source identity differs from frozen source")
            self.ledger["family_setup"].setdefault(key, {})[name] = setup
            self.prepared.add((name, key))

    def _verify_worker(self, name, key, case):
        verified = self.workers[name].request({"action": "family-verify", "case": case,
                                               "device": self.device, "verify_environment": True})
        if verified["source_sha256"] != self.identity["receiver_source_sha256"][name][key]:
            raise ValueError("family worker source identity changed")
        if verified.get("environment_matches_setup") is not True:
            raise ValueError("family worker execution environment changed during measurement")
        return verified

    def _claim_seed(self, seed, count, *, phase, profile):
        if type(seed) is not int or not 0 <= seed < 2**63 - count:
            raise ValueError("family generation seed must be a nonnegative bounded integer")
        seeds = set(range(seed, seed + count))
        profile_hash = sample.digest(profile)
        previous = (self.generation_seeds if phase == "confirmation" else
                    self.confirmation_seeds | self.screen_profile_seeds.get(profile_hash, set()))
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
            self.screen_profile_seeds.setdefault(profile_hash, set()).update(seeds)
        self.generation_seeds.update(seeds)
        self.ledger["generation_seed_claims"].append({
            "phase": phase, "profile_sha256": profile_hash, "seed": seed,
            "count": count, "capture_seeds": sorted(seeds)})

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
        return pair, {"generation_shared": True, "generation_group_created": created,
                      "actual_generation_seed": group["seed"],
                      "requested_seed_used_for_generation": created,
                      "independent_complete_pipeline_observation": False}

    def _confirmation(self, key, case, run_id, seed, order):
        self._claim_seed(seed, case["captures"], phase="confirmation",
                         profile=self.manifest["fixtures"][case["fixture"]])
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
                      "independent_complete_pipeline_observation": True,
                      "fixture_cleanup_outside_target_timer": True}

    def measure_pair(self, case_id, run_id, seed, order=SOURCES, phase="screen"):
        start = time.perf_counter()
        outcome = "completed"
        try:
            return self._measure_pair(case_id, run_id, seed, order, phase)
        except Exception as exc:
            outcome = type(exc).__name__
            raise
        finally:
            self.ledger["operations"].append({"run_id": run_id, "case_id": case_id,
                                               "phase": phase, "outcome": outcome,
                                               "active_wall_seconds": time.perf_counter() - start})

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
        self._ensure_workers()
        case = self.cases[case_id]
        self._prepare(case_id, case, order)
        raw_path = self.output / ("pair-" + sample.digest(run_id)[:24] + ".json")
        try:
            if phase == "screen":
                pair, metadata = self._screen(case_id, case, seed, order)
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
            field = "screen_estimated_full_seconds" if phase == "screen" else "complete_pipeline_wall_seconds"
            result = {
                "run_id": run_id,
                **{name + "_seconds": _positive_seconds(pair[name][field], field) for name in SOURCES},
                "details": {"case_id": case_id, "phase": phase, "boundary": BOUNDARY,
                            "full_fresh": phase == "confirmation", "full_work": True,
                            "timing_field": field, "metadata": metadata, "pair": pair,
                            "order": list(order), "requested_seed": seed,
                            "work_signatures": signatures, "numerical_agreement": agreement,
                            "comparison_work_signatures": comparable_signatures,
                            "frozen_work_signatures": frozen_signatures,
                            "cross_revision_private_allocations": ignored_allocations,
                            "cross_revision_allocation_contract": ALLOCATION_COMPARISON_CONTRACT,
                            "library_enumeration_contract": LIBRARY_ENUMERATION_CONTRACT,
                            "source_checks": verified, "identity_before": before,
                            "identity_after": after, "started": started, "finished": _stamp(),
                            "raw_receipt": str(raw_path)},
            }
            self.ledger["measurements"].append({"run_id": run_id, "case_id": case_id,
                                               "phase": phase, "receipt": str(raw_path),
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
                       for row in result["measurements"] if row["phase"] == "screen"
                       for name in SOURCES)
        result["shared_generation_accounting"] = {
            "measured_native_invocation_seconds": actual,
            "estimated_repeated_generation_seconds_for_completed_screens": repeated,
            "estimated_native_invocation_seconds_saved": repeated - actual,
            "scope": "screening only; additive observed invocation costs, not an independent full campaign prediction; a partially visited capture group may have negative savings",
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
            for worker in self.workers.values():
                try:
                    worker.close()
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
