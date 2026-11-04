"""Full-target planner and memory-boundary observations for the fast guard.

This adapter makes no statistical decision and uses no extrapolation model.
The runner freezes its inventory, screens once, and requests fresh pairs only
for suspects. Nested operation timers are diagnostics, never extra hypotheses.
Existing qualification evidence and its failed duration fits are untouched.
"""
from __future__ import annotations

import copy
import importlib.metadata
import math
import os
from pathlib import Path
import subprocess
import sys
import time

import triad_perf_planner as planner
import triad_perf_resource as resource

SIDES = ("reference", "candidate")
BUDGETS_MIB = {"paired": 5, "sequential": 4}
RESOURCE_BOUNDARY = (
    "complete native grouped-statistics subprocess through exit: generation, "
    "two channel passes, grouped preparation, output writes and finalization; "
    "configure probes, validation hashes and scratch cleanup excluded"
)


class ExtraUnavailable(RuntimeError):
    """Declared workload cannot run because an optional input was not supplied."""


def _path(value):
    return None if value is None else Path(value).resolve()


def _positive_seconds(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError("complete-process timer must be finite and positive")
    return value


class ExtraWorkloads:
    """Ten declared direct-full jobs; initialization never executes workloads.

    Native resource exports are serial, and Gaussian design bounds retain their
    existing two-thread CPU contract, regardless of the main receiver backend.
    Supply the same screen seed to both resource modes to check cross-regime
    byte parity without an extra export. ``summary`` exposes missing parity.
    """

    def __init__(self, context):
        self.context = dict(context)
        roots = context.get("roots", {})
        binaries = context.get("binaries", {})
        self.roots = {side: _path(roots.get(side, context.get(side + "_root"))) for side in SIDES}
        self.binaries = {"candidate": _path(binaries.get("candidate", context.get("binary"))),
                         "reference": _path(binaries.get("reference", context.get("reference_binary")))}
        self.planner_binaries = {"candidate": _path(context.get("planner_binary")),
                                 "reference": _path(context.get("planner_reference_binary"))}
        self.workspace = _path(context.get("planner_bounds_workspace"))
        self.output = Path(context["output"]) / "extras"
        self.deadline = context["deadline"]
        self.threads = context.get("native_threads", 1)
        self.manifest = planner.manifest()
        self.resource_config = resource.load_config()
        self._closed = False
        self._runs = set()
        self._measured = set()
        self._resource_geometry = None
        self._resource_regimes = {}
        self._resource_records = {}
        self._cross_checks = []
        self._rows = []
        self._identity = {}
        self._cases = {}
        self._available = {}

        self._available["native-planner"] = self._inputs(self.roots, self.planner_binaries)
        self._available["gaussian-bounds"] = self._inputs(self.roots, {"workspace": self.workspace})
        self._available["native-resource"] = self._inputs(self.roots, self.binaries)
        if self._available["native-resource"][0] and self.threads != 1:
            raise ValueError("native resource targets require native_threads=1 (serial exporter contract)")
        for kind, (available, _) in self._available.items():
            if available:
                self._identity[kind] = self._read_identity(kind)

        for case in self.manifest["cases"]:
            self._cases["planner:" + case["id"]] = ("native-planner", copy.deepcopy(case))
        for case in self.manifest["bounds_cases"]:
            self._cases["planner:" + case["id"]] = ("gaussian-bounds", copy.deepcopy(case))
        for mode in resource.REGIMES:
            self._cases["resource:" + mode] = ("native-resource", {"mode": mode})

    @staticmethod
    def _inputs(*groups):
        missing = []
        for group in groups:
            for name, path in group.items():
                if path is None:
                    missing.append(name)
                elif not path.exists():
                    raise ValueError("configured extras input does not exist: " + str(path))
        return (not missing, None if not missing else "required input omitted: " + ", ".join(missing))

    def _read_identity(self, kind):
        if kind == "native-planner":
            return planner.identities(self.roots["candidate"], self.roots["reference"],
                                      self.planner_binaries["candidate"], self.planner_binaries["reference"])
        if kind == "gaussian-bounds":
            # Same public helper contract, without requiring unrelated native
            # planner binaries merely to run the retained-workspace CLI.
            return {"workspace": planner.workspace_identity(self.workspace),
                    "sources": {side: {name: planner.file_sha(self.roots[side] / "tools" / name)
                                       for name in planner.BOUNDS_DEPENDENCIES} for side in SIDES},
                    "libraries": {name: importlib.metadata.version(name) for name in ("torch", "numpy")},
                    "python_sha256": planner.file_sha(sys.executable), "threads": 2, "device": "cpu",
                    "arithmetic": "complex128", "explicit_environment_controls": dict(planner.BOUNDS_THREAD_ENV),
                    "inherited_environment_controls": {name: os.environ.get(name) for name in planner.BOUNDS_INHERITED_ENV},
                    "manifest_sha256": planner.file_sha(planner.MANIFEST),
                    "adapter_sha256": planner.file_sha(planner.__file__)}
        return {"implementation": resource.implementation_identity(),
                "sources": {side: resource.binary_identity(self.binaries[side], self.roots[side], self.threads)
                            for side in SIDES}}

    def descriptors(self):
        rows = []
        for case_id, (kind, case) in self._cases.items():
            available, reason = self._available[kind]
            boundary = (RESOURCE_BOUNDARY if kind == "native-resource" else
                        self.manifest["bounds_timing_boundary" if kind == "gaussian-bounds" else "timing_boundary"])
            work = (dict(self.resource_config, memory_mib=BUDGETS_MIB[case["mode"]], mode=case["mode"])
                    if kind == "native-resource" else case)
            rows.append({"id": case_id, "kind": kind, "boundary": boundary,
                         "available": available,
                         "availability": "available" if available else "unavailable", "reason": reason,
                         "method": "direct_full", "model_used": False, "backend": "cpu",
                         "threads": 2 if kind == "gaussian-bounds" else 1,
                         "workload": copy.deepcopy(work), "identity": copy.deepcopy(self._identity.get(kind)),
                         "inner_operation_timer": "diagnostic_only",
                         "unmeasured_scaling_claim": False})
        return rows

    def _resource_setup(self):
        if self._resource_geometry is None:
            # Match resource.direct's qualified shared-calendar contract. A
            # historical reference can predate configure's geometry metadata;
            # both completed exports must still emit and match all actual work
            # counters. The candidate probe supplies expected work, never a
            # substitute measurement of either source's emitted work.
            expected = resource.target_geometry(self.binaries["candidate"], self.resource_config,
                       threads=self.threads, deadline=self.deadline)
            self._resource_geometry = {side: copy.deepcopy(expected) for side in SIDES}
            planner.write_json(self.output / "resource-geometry.json", {
                "expected_geometry_source": "candidate_configure",
                "expected_geometry": expected,
                "actual_geometry_source": "each_complete_export_native_counters",
                "verification": "both reference and candidate exports must match the same frozen expected geometry"})

    def _resource_pair(self, case, run_dir, seed, order):
        self._resource_setup()
        mode = case["mode"]
        budget = BUDGETS_MIB[mode]
        records = {}
        for side in order:
            row = resource.run_export(self.binaries[side], self.resource_config,
                    self.resource_config["target_seconds"], seed, budget, mode, run_dir,
                    deadline=self.deadline, threads=self.threads,
                    failure_path=run_dir / (side + "-failure.json"))
            planner.write_json(run_dir / (side + "-raw.json"), row)
            resource.verify_counter_record(row, self.resource_config, mode=mode, memory_mib=budget,
                                           geometry=self._resource_geometry[side])
            if row["requested_seconds"] != self.resource_config["target_seconds"] or row["seed"] != seed:
                raise ValueError("resource target duration or physical seed changed")
            # The paired allocation must really exceed the sequential budget;
            # otherwise these runs no longer straddle the frozen 5/4 MiB edge.
            if mode == "paired" and not 4 * resource.MIB < row["regime"]["bounded_workspace_bytes"] <= 5 * resource.MIB:
                raise ValueError("resource paired allocation no longer straddles the fixed 5/4 MiB boundary")
            frozen = self._resource_regimes.setdefault((side, mode), copy.deepcopy(row["regime"]))
            if row["regime"] != frozen:
                raise ValueError("resource actual computational regime changed between pairs")
            records[side] = row
        parity = resource.compare_outputs(records["reference"], records["candidate"])
        for side in SIDES:
            other = "sequential" if mode == "paired" else "paired"
            peer = self._resource_records.get((side, other, seed))
            if peer is not None:
                checked = resource.compare_outputs(peer, records[side], across_regimes=True)
                self._cross_checks.append({"side": side, "seed": seed, **checked})
            self._resource_records[(side, mode, seed)] = records[side]
        return {side: row["full_test_wall_seconds"] for side, row in records.items()}, {
            "raw": records, "numerical_agreement": parity, "geometry": self._resource_geometry,
            "expected_geometry_source": "candidate_configure",
            "actual_geometry_source": "each_complete_export_native_counters",
            "fixed_memory_mib": budget, "duration_model_used": False,
            "cross_regime_parity": copy.deepcopy(self._cross_checks)}

    def measure_pair(self, case_id, run_id, seed, order=SIDES, phase="screen"):
        if self._closed:
            raise ValueError("extras adapter is closed")
        if case_id not in self._cases:
            raise ValueError("unknown extras workload: " + str(case_id))
        if tuple(order) not in (SIDES, tuple(reversed(SIDES))):
            raise ValueError("a pair must contain reference and candidate exactly once")
        if type(run_id) not in (str, int) or not str(run_id) or type(seed) is not int or seed < 0:
            raise ValueError("invalid extras run identity or physical seed")
        if phase not in ("screen", "confirmation"):
            raise ValueError("unknown extras measurement phase")
        key = (case_id, str(run_id))
        if key in self._runs:
            raise ValueError("extras run identity already consumed; confirmation needs fresh processes")
        kind, case = self._cases[case_id]
        available, reason = self._available[kind]
        if not available:
            raise ExtraUnavailable(case_id + ": " + reason)
        if self.deadline <= time.monotonic():
            raise TimeoutError("extras workload deadline exhausted")
        self._runs.add(key)
        self._measured.add(kind)
        run_dir = self.output / (case_id.replace(":", "-") + "-" + planner.digest([run_id, phase])[:16])
        run_dir.mkdir(parents=True, exist_ok=False)
        period_started = time.perf_counter()
        try:
            if kind == "native-resource":
                seconds, details = self._resource_pair(case, run_dir, seed, order)
            else:
                observations = {}
                for side in order:
                    raw = run_dir / (side + "-raw.json")
                    if kind == "native-planner":
                        observation = planner.invoke(self.planner_binaries[side], case,
                            self._identity[kind]["sources"][side], self.deadline, raw)
                        planner.validate_native(observation["native"], case, self._identity[kind]["sources"][side])
                    else:
                        observation = planner.invoke_bound(self.roots[side], self.workspace, case,
                            self._identity[kind], side, self.deadline, raw)
                        planner.validate_bound(observation["bounds"], case, self._identity[kind], side)
                    if planner.operation_seconds(observation) > observation["wall_seconds"] + .001:
                        raise ValueError("inner operation timer exceeds complete-process timer")
                    observations[side] = observation
                if kind == "native-planner":
                    planner.numerical_agreement(observations["reference"]["native"], observations["candidate"]["native"])
                else:
                    planner.bounds_agreement(observations["reference"]["bounds"], observations["candidate"]["bounds"])
                seconds = {side: observations[side]["wall_seconds"] for side in SIDES}
                details = {"raw": observations, "numerical_agreement": True,
                           "operation_seconds_diagnostic": {side: planner.operation_seconds(observations[side]) for side in SIDES},
                           "fixture_generation_in_timing_target": False}
            if phase == "confirmation":
                # Each invoke/run_export above starts a new subprocess and waits
                # through its exit. Identify that launch by its unique raw
                # receipt, rather than pretending subprocess.run exposes a PID.
                details["confirmation_context"] = {
                    "protocol": "fresh-source-context-pair-v1",
                    "creation_order": list(order),
                    "context_ids": {side: str(run_dir / (side + "-raw.json")) for side in SIDES},
                    "identity_kind": "invocation-owned-fresh-subprocess",
                    "isolation_evidence": {side: {
                        "raw_launch_receipt": str(run_dir / (side + "-raw.json")),
                        "argv": details["raw"][side].get("argv"),
                        "launch_api": "subprocess.run; complete exit before next source",
                    } for side in SIDES},
                    "target_boundary_preserved": True,
                    "warmup_policy": "fresh-process-target-includes-startup",
                    "setup_seconds": 0., "warmup_seconds": 0., "teardown_seconds": 0.,
                    "cost_boundary": "startup and exit already included in each complete-process target; configure/validation overhead separate",
                    "period_wall_seconds": time.perf_counter() - period_started,
                }
            result = {"run_id": run_id, "reference_seconds": _positive_seconds(seconds["reference"]),
                      "candidate_seconds": _positive_seconds(seconds["candidate"]),
                      "details": {"case_id": case_id, "phase": phase, "seed": seed, "order": list(order),
                                  "identity": self._identity[kind], "method": "direct_full", "model_used": False,
                                  "performance_hypothesis": "complete_process_only", **details}}
            planner.write_json(run_dir / "pair.json", result)
            self._rows.append({"case_id": case_id, "run_id": run_id, "phase": phase,
                               "path": str(run_dir / "pair.json")})
            return result
        except Exception as exc:
            planner.write_json(run_dir / "error.json", {"case_id": case_id, "run_id": run_id,
                "phase": phase, "error": str(exc), "error_type": type(exc).__name__})
            if isinstance(exc, subprocess.TimeoutExpired):
                raise TimeoutError("extras subprocess exceeded its workload deadline") from exc
            if isinstance(exc, resource.ResourcePerformanceError):
                raise ValueError(str(exc)) from exc
            raise

    def close(self):
        if self._closed:
            return
        for kind in self._measured:
            if self._read_identity(kind) != self._identity[kind]:
                raise ValueError("extras source/build/input/environment changed during measurements: " + kind)
        self._closed = True

    def summary(self):
        checked = {row["side"] for row in self._cross_checks}
        return {"pairs": copy.deepcopy(self._rows), "closed": self._closed,
                "cross_regime_parity": copy.deepcopy(self._cross_checks),
                "resource_cross_regime_functional": "passed" if checked == set(SIDES) else "unverified",
                "resource_cross_regime_limit": None if checked == set(SIDES) else
                    "Run both memory regimes with the same screen seed; unmatched seeds cannot establish byte parity.",
                "unsupported": [{"id": row["id"], "reason": row["reason"]}
                                for row in self.descriptors() if row["availability"] == "unavailable"],
                "statistical_decisions": "runner_owned; no nested operation gate and no duration extrapolation"}
