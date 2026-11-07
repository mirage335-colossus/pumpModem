#!/usr/bin/env python3
"""Fast, reject-only performance checks for the isolated three-carrier modem.

Screen every declared workload once, then spend fresh complete-target pairs only
on suspects. Statistical inconclusiveness, historical model applicability and
one-block hardware warnings are advisory. Correctness/integrity failures and
suite-budget-confirmed regressions remain blocking. A zero exit is never a
non-regression certificate or a complete qualification result.
"""
from __future__ import annotations

import time
ENTRY_STARTED = time.perf_counter()

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import secrets
import re
import signal
import sys
import traceback
import uuid

import triad_perf_complete as complete
from triad_perf_guard_policy import (GuardPolicy, PairObservation, BlockObservation, build_guard_plan,
                                     build_guard_report, confirm_case, confirmation_futility,
                                     screen_case)


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-performance-fast-guard-run-v2"
CRYPTO_SOURCES = {"src/crypto.cpp", "include/datapump/crypto.hpp"}
SERPENT_SOURCES = {"src/serpent.cpp", "include/datapump/serpent.hpp"}


def _json_default(value):
    if isinstance(value, Path):
        return str(value)
    if hasattr(value, "item"):
        return value.item()
    if hasattr(value, "tolist"):
        return value.tolist()
    raise TypeError("unsupported evidence value: " + type(value).__name__)


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False, default=_json_default) + "\n")
    temporary.replace(path)


def _event(path, value):
    with Path(path).open("a") as stream:
        stream.write(json.dumps(value, allow_nan=False, default=_json_default) + "\n")


def _sha(path):
    return complete.digest(Path(path))


def _legacy_api():
    name = "_triad_fast_guard_legacy_api"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, ROOT / "tools/triad-performance.py")
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


def resolve_device(requested):
    from triad_perf_hardware import discover_hardware
    if requested == "auto":
        gpu = discover_hardware("cuda", threads=1)
        return ("cuda", None) if gpu["device"]["status"] == "available" else (
            "cpu", "CUDA unavailable; auto selected CPU, not GPU performance coverage")
    descriptor = discover_hardware(requested, threads=1)
    if descriptor["device"]["status"] != "available":
        raise ValueError("explicitly requested execution backend is unavailable: " + requested)
    return requested, None


def source_snapshot(candidate, reference, binaries):
    """Record existing declared files; historical roots may predate harness tests."""
    matrix = complete.read_json(ROOT / "tools/triad-performance-coverage.json")
    families = complete.read_json(ROOT / "tools/triad-performance-families.json")
    names = {"CMakeLists.txt"} | CRYPTO_SOURCES | SERPENT_SOURCES
    for row in matrix["rows"]:
        names.update(row["source_files"])
        names.update(test.split("::", 1)[0] for test in row["cheap_functional_tests"]
                     if test.startswith(("tests/", "tools/")))
    for case in families["cases"]:
        names.update("tools/" + name for name in case["dependencies"])
    paths, missing, absent_paths = {}, {}, {}
    for label, root in (("candidate", Path(candidate)), ("reference", Path(reference))):
        absent = []
        selected = names | {str(p.relative_to(root)) for p in (root / "cmake").rglob("*.cmake")}
        for name in sorted(selected):
            path = (root / name).resolve()
            if not path.is_relative_to(root):
                raise ValueError("source dependency escapes root: " + name)
            if path.is_file():
                paths[label + ":" + name] = path
            else:
                if name in CRYPTO_SOURCES or (name in SERPENT_SOURCES and label == "candidate"):
                    raise ValueError("missing required " + label + " source dependency: " + name)
                if name in SERPENT_SOURCES:
                    absent_paths[label + ":" + name] = str(path)
                absent.append(name)
        missing[label] = absent
    # Bind the actual execution closure, not unrelated offline statistical
    # research scripts that happen to share a filename prefix.
    harness = {ROOT / "tools" / name for name in (
        "triad_perf_guard.py", "triad_perf_guard_policy.py",
        "triad_perf_guard_native_fft.py", "triad_perf_guard_families.py",
        "triad_perf_guard_extras.py")}
    harness.update(ROOT / "tools" / name for name in (
        "triad_perf_complete.py", "triad_perf_hardware.py", "triad-performance.py",
        "triad-performance-coverage.json", "triad-performance-families.json",
        "triad-performance-native-cases.json", "triad-performance-cases.json"))
    for path in sorted(harness):
        paths["harness:" + str(path.relative_to(ROOT))] = path
    for name, path in binaries.items():
        if path is not None:
            paths["binary:" + name] = Path(path)
    return {"sha256": {name: _sha(path) for name, path in paths.items()},
            "paths": {name: str(path) for name, path in paths.items()},
            "absent_paths": absent_paths,
            "missing_declared_historical_files": missing,
            "scope": "current files bound before/after; loaded worker dependencies and full-work checks are additionally verified by each adapter"}


def verify_snapshot(snapshot):
    changed = []
    for name, filename in snapshot["paths"].items():
        path = Path(filename)
        if not path.is_file() or _sha(path) != snapshot["sha256"][name]:
            changed.append(name)
    for name, filename in snapshot.get("absent_paths", {}).items():
        if Path(filename).exists():
            changed.append(name)
    return {"status": "passed" if not changed else "failed", "changed": changed}


def collect_descriptors(adapters, requested=()):
    descriptors, owners = [], {}
    for label, adapter in adapters:
        for descriptor in adapter.descriptors():
            if not isinstance(descriptor, dict):
                raise ValueError("workload descriptor must be an object")
            name = descriptor.get("id")
            if not isinstance(name, str) or not name or name in owners:
                raise ValueError("duplicate or invalid workload ID: " + str(name))
            if not isinstance(descriptor.get("boundary"), str) or not descriptor["boundary"]:
                raise ValueError("workload has no declared complete timing boundary: " + name)
            if "available" in descriptor and type(descriptor["available"]) is not bool:
                raise ValueError("available must be a boolean: " + name)
            descriptors.append({**descriptor, "adapter": label})
            owners[name] = adapter
    if requested:
        if len(set(requested)) != len(requested) or set(requested) - set(owners):
            raise ValueError("--case requires unique declared workload IDs")
        descriptors = [row for row in descriptors if row["id"] in set(requested)]
        owners = {row["id"]: owners[row["id"]] for row in descriptors}
    if not descriptors:
        raise ValueError("guard requires a nonempty declared workload family")
    return descriptors, owners


def measurement_schedule(ids, policy, *, seed, session, looks=None, order_seed=None):
    """Freeze fair block coins, opposite assignments and disjoint capture ranges."""
    looks = policy.looks if looks is None else tuple(looks)
    if not looks or any(type(n) is not int or n < 1 for n in looks):
        raise ValueError("measurement schedule needs positive frozen look counts")
    generator = random.Random(order_seed) if order_seed is not None else None
    def draw_coin():
        return generator.getrandbits(1) if generator is not None else secrets.randbits(1)
    result = {}
    for case_index, name in enumerate(ids):
        screen_order = ["reference", "candidate"] if draw_coin() == 0 else ["candidate", "reference"]
        blocks = []
        for index in range(max(looks)):
            coin = draw_coin()
            first = ["reference", "candidate"] if coin == 0 else ["candidate", "reference"]
            block_id = session + ":confirmation:" + str(case_index) + ":" + str(index)
            block_seed = seed + 1_000_000 + 16 * (case_index * max(looks) + index)
            periods = [{"run_id": block_id + ":period:" + str(period),
                        "seed": block_seed + 8 * period,
                        "order": first if period == 0 else first[::-1]}
                       for period in range(2)]
            blocks.append({"run_id": block_id, "coin": coin,
                           "seed": block_seed, "order": first, "periods": periods})
        result[name] = {"screen": {"run_id": session + ":screen:" + str(case_index),
                                  "seed": seed, "order": screen_order},
                        "confirmation": blocks}
    return result


def _observation(raw, expected_id):
    if not isinstance(raw, dict) or raw.get("run_id") != expected_id:
        raise ValueError("adapter returned a missing or different outer run identity")
    if not isinstance(raw.get("details"), dict):
        raise ValueError("adapter omitted work/integrity evidence")
    return PairObservation(raw["run_id"], raw["reference_seconds"], raw["candidate_seconds"])


def _context_receipt(raw, order, used_contexts):
    """A missing experimental-unit proof is nonblocking missing assurance.

    Source/work/numerical corruption is still handled by the adapter. This
    check prevents a complete but permanently confounded timing from being
    promoted into statistically eligible evidence merely by assigning an ID.
    """
    evidence = raw["details"].get("confirmation_context")
    if not isinstance(evidence, dict):
        return None, "fresh execution-context evidence is absent"
    if (evidence.get("protocol") != "fresh-source-context-pair-v1" or
            evidence.get("creation_order") != list(order) or
            evidence.get("target_boundary_preserved") is not True or
            evidence.get("warmup_policy") not in ("full-target-outside-timer", "fresh-process-target-includes-startup")):
        return None, "execution-context protocol or original target boundary is unverified"
    identities = evidence.get("context_ids", {})
    isolation = evidence.get("isolation_evidence", {})
    if (not isinstance(identities, dict) or set(identities) != {"reference", "candidate"} or
            not isinstance(isolation, dict) or set(isolation) != {"reference", "candidate"} or
            not evidence.get("identity_kind") or not all(isolation.values())):
        return None, "context identities lack actual isolation/launch receipts"
    values = tuple(identities[name] for name in ("reference", "candidate"))
    if (any(not isinstance(value, str) or not value.strip() for value in values) or
            len(set(values)) != 2 or set(values) & used_contexts):
        return None, "execution contexts were reused or their identities are invalid"
    used_contexts.update(values)
    return values, None


def exercise_workloads(adapters, descriptors, owners, plan, schedule, *, deadline, output,
                       clock=time.monotonic):
    """Broad screens followed by bounded, round-robin randomized blocks."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    screens, confirmations, futilities, failures, advisory, measurements = [], [], [], [], [], []
    disabled, used_contexts = set(), set()
    screen_wall = {}
    profile = plan["policy"]["profile"]

    def measure(name, invocation, phase):
        start = clock()
        receipt = {"hypothesis_id": name, "phase": phase,
                   "plan_sha256": plan["record_sha256"], "invocation": invocation,
                   "status": "running", "started_monotonic": start}
        _event(output / "events.jsonl", receipt)
        try:
            raw = owners[name].measure_pair(name, invocation["run_id"], invocation["seed"],
                order=tuple(invocation["order"]), phase=phase)
            pair = _observation(raw, invocation["run_id"])
            json.dumps(raw, allow_nan=False, default=_json_default)
            receipt.update(status="completed", observation=raw)
            return pair, receipt
        except TimeoutError as error:
            receipt.update(status="budget_limited", error=str(error))
            advisory.append(name + ": budget/timeout limited " + phase + "; no statistical rejection inferred")
            return None, receipt
        except (KeyboardInterrupt, SystemExit) as error:
            receipt.update(status="interrupted", error=type(error).__name__ + ": " + str(error))
            raise
        except Exception as error:
            receipt.update(status="execution_or_integrity_failure",
                error=type(error).__name__ + ": " + str(error), traceback=traceback.format_exc())
            failures.append(name + ": " + receipt["error"])
            disabled.add(id(owners[name]))
            return None, receipt
        finally:
            receipt["wall_seconds"] = max(0., clock() - start)
            measurements.append(receipt)
            receipt["measurement_file"] = "pair-%04d.json" % (len(measurements) - 1)
            write_json(output / receipt["measurement_file"], receipt)
            _event(output / "events.jsonl", receipt)

    for descriptor in descriptors:
        name = descriptor["id"]
        available = descriptor.get("available", descriptor.get("availability") != "unavailable")
        if not available:
            advisory.append(name + ": unavailable: " + str(descriptor.get("reason", "unsupplied optional configuration")))
            continue
        if id(owners[name]) in disabled:
            advisory.append(name + ": not run after this adapter failed; coverage remains limited")
            continue
        if clock() >= deadline:
            advisory.append(name + ": screen not started before the shared budget expired")
            continue
        pair, receipt = measure(name, schedule[name]["screen"], "screen")
        if pair is not None:
            screens.append(screen_case(plan, name, pair))
            screen_wall[name] = max(receipt["wall_seconds"], pair.reference_seconds + pair.candidate_seconds)
    screen_by_id = {row["hypothesis_id"]: row for row in screens}
    if plan["metadata"].get("randomization_inference_eligible") is False:
        advisory.append("deterministic order replay is diagnostic only; fresh randomized-block inference was disabled")
        return screens, confirmations, futilities, failures, advisory, measurements
    # Cheap nominees get an opportunity first, then every active nominee gets
    # at most one complete block per round. One uncertain case cannot consume
    # a whole twelve-block look before other cases receive confirmation work.
    active = sorted([name for name, value in screen_by_id.items()
                     if value["nominate_confirmation"] and id(owners[name]) not in disabled],
                    key=lambda name: screen_wall[name])
    rows_by_id = {name: [] for name in active}
    estimates = {name: 2.5 * screen_wall[name] for name in active}
    block_index = 0
    while active:
        for name in list(active):
            if id(owners[name]) in disabled:
                advisory.append(name + ": confirmation unavailable after this adapter failed")
                active.remove(name)
                continue
            remaining = deadline - clock()
            if remaining <= 0 or estimates[name] > remaining:
                advisory.append(name + ": next complete confirmation block not started; estimated %.3fs, remaining %.3fs" % (estimates[name], max(0., remaining)))
                active.remove(name)
                continue
            rows = rows_by_id[name]
            invocation = schedule[name]["confirmation"][len(rows)]
            started = clock()
            attempted = {"hypothesis_id": name, "run_id": invocation["run_id"],
                         "coin": invocation["coin"], "periods": [], "status": "running",
                         "plan_sha256": plan["record_sha256"]}
            pairs, contexts = [], []
            for period in invocation["periods"]:
                if clock() >= deadline:
                    attempted.update(status="budget_limited", reason="shared budget expired within an incomplete block")
                    advisory.append(name + ": incomplete confirmation block retained; no sign or rejection inferred")
                    break
                pair, receipt = measure(name, period, "confirmation")
                attempted["periods"].append({"invocation": period, "measurement_file": receipt["measurement_file"],
                                             "status": receipt["status"]})
                if pair is None:
                    attempted.update(status=receipt["status"], reason="confirmation period did not complete")
                    break
                context_ids, reason = _context_receipt(receipt["observation"], period["order"], used_contexts)
                if reason:
                    attempted.update(status="ineligible_context", reason=reason)
                    advisory.append(name + ": confirmation context ineligible: " + reason + "; no statistical rejection inferred")
                    break
                pairs.append(pair)
                contexts.append(context_ids)
            attempted["wall_seconds"] = max(0., clock() - started)
            if len(pairs) == 2:
                observation = BlockObservation(invocation["run_id"], tuple(pairs),
                    tuple(tuple(period["order"]) for period in invocation["periods"]), tuple(contexts))
                rows.append(observation)
                attempted.update(status="completed", observation={"run_id": observation.run_id,
                    "pairs": [dict(run_id=p.run_id, reference_seconds=p.reference_seconds,
                                   candidate_seconds=p.candidate_seconds) for p in observation.pairs],
                    "orders": observation.orders, "contexts": observation.contexts},
                    reference_seconds=observation.reference_seconds, candidate_seconds=observation.candidate_seconds)
                estimates[name] = 1.25 * attempted["wall_seconds"]
            write_json(output / ("block-%04d.json" % block_index), attempted)
            _event(output / "blocks.jsonl", attempted)
            block_index += 1
            if len(pairs) != 2:
                active.remove(name)
                continue
            screen_id = screen_by_id[name]["observation"]["run_id"]
            if len(rows) in plan["looks"]:
                decision = confirm_case(plan, name, rows, screen_run_ids=[screen_id])
                confirmations.append(decision)
                if decision["confirmed_regression"]:
                    active.remove(name)
                    if profile == "ci":
                        advisory.append("CI stopped further confirmations after its first confirmed regression; every available screen had already run")
                        return screens, confirmations, futilities, failures, advisory, measurements
                    continue
            if len(rows) < plan["looks"][-1]:
                stop = confirmation_futility(plan, name, rows, screen_run_ids=[screen_id])
                if stop["futile"]:
                    futilities.append(stop)
                    write_json(output / ("futility-%04d.json" % (len(futilities)-1)), stop)
                    active.remove(name)
                    continue
            if len(rows) == plan["looks"][-1]:
                active.remove(name)
    return screens, confirmations, futilities, failures, advisory, measurements


def _required_methods(matrix, command):
    filename = next((Path(arg).name for arg in command if arg.endswith(".py")), None)
    if filename is None:
        return []
    return sorted({test.split("::", 1)[1] for row in matrix["rows"]
                   for test in row["cheap_functional_tests"]
                   if test.startswith("tests/" + filename + "::")})


def _completed_failure_before_timeout(command, receipt):
    """A later deadline does not erase a completed correctness failure."""
    if receipt.get("status") != "deadline_exceeded":
        return False
    output = complete.command_output(receipt)
    if Path(command[0]).name == "ctest":
        return bool(re.search(r"\*\*\*(?:Failed|Exception)", output))
    return bool(re.search(r"(?m)^FAILED\s*\((?:failures|errors)=\d+", output))


def run_functional(args, candidate, binary, matrix, output, deadline, device):
    receipts, failures, advisory = [], [], []
    environment = complete.child_environment(args.threads)
    if args.functional_defaults:
        command = ["ctest", "--test-dir", str(binary.parent), "-N", "--show-only=json-v1", "-R", complete.NATIVE_TESTS]
        receipt = complete.execute(command, cwd=candidate, directory=output / "native-inventory",
                                   deadline=deadline, env=environment)
        inventory = complete.native_inventory(receipt)
        receipts.append({"kind": "native_inventory", "command": receipt, "evidence": inventory})
        if receipt["status"] in ("deadline_exceeded", "not_started_budget_exhausted"):
            advisory.append("required native test inventory was budget-limited; functional scope is incomplete")
        elif inventory["status"] != "passed":
            failures.append("required native functional tests are missing, duplicated, disabled or unenumerated")
    commands = complete.functional_defaults(candidate, binary, matrix, device) if args.functional_defaults else []
    commands.extend(args.functional_command)
    if not commands:
        advisory.append("standalone functional commands were not requested; adapter numerical/work integrity remains checked")
    for index, command in enumerate(commands):
        receipt = complete.execute(command, cwd=candidate, directory=output / ("functional-%02d" % index),
                                   deadline=deadline, env=environment)
        evidence = complete.functional_evidence(command, receipt, device, _required_methods(matrix, command))
        receipts.append({"kind": "functional", "command": receipt, "evidence": evidence})
        if receipt["status"] in ("deadline_exceeded", "not_started_budget_exhausted"):
            if _completed_failure_before_timeout(command, receipt):
                failures.append("completed functional failure preceded a later deadline: " + " ".join(command))
                evidence["completed_failure_before_timeout"] = True
            else:
                advisory.append("functional command budget-limited: " + " ".join(command))
        elif evidence["status"] != "passed":
            failures.append("functional execution/evidence failed: " + " ".join(command))
    return receipts, failures, advisory


def check_coverage_receipts(args, candidate, matrix):
    rows, advisory = [], []
    for kind, filename in (("native", args.native_coverage), ("python", args.python_coverage)):
        if filename is None:
            advisory.append(kind + " instrumented coverage receipt was not supplied")
            continue
        try:
            expected = matrix["coverage_reports"][kind]["source_files"]
            dependencies = complete.coverage_dependencies(candidate, matrix, expected, kind)
            receipt = complete.check_coverage(filename, candidate, expected, dependencies)
            rows.append({"kind": kind, **receipt})
            if receipt["historical_applicability"] != "declared_source_and_dependency_match":
                advisory.append(kind + " retained coverage is inapplicable or incomplete; no coverage pass inferred")
        except (ValueError, OSError, KeyError) as error:
            rows.append({"kind": kind, "status": "unavailable", "error": type(error).__name__ + ": " + str(error)})
            advisory.append(kind + " coverage could not be validated: " + str(error))
    return rows, advisory


def adapter_factories():
    from triad_perf_guard_native_fft import NativeFFTWorkloads
    from triad_perf_guard_families import FamilyWorkloads
    from triad_perf_guard_extras import ExtraWorkloads
    return [("native_fft", NativeFFTWorkloads), ("families", FamilyWorkloads), ("extras", ExtraWorkloads)]


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("ci", "gpu-dev"), default="ci")
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--reference-binary", type=Path, required=True)
    parser.add_argument("--fixture-cache", type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda", "auto"))
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32",
                        help="sampled-family format; FFT fp32 is a minimum with eligible FP64 allowed, fp64 forces FP64")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--budget-seconds", type=float)
    parser.add_argument("--suite-alpha", type=float)
    parser.add_argument("--workflow-job-count", type=int, default=1)
    parser.add_argument("--workflow-job-index", type=int, default=0)
    parser.add_argument("--seed", type=int, default=724031)
    parser.add_argument("--order-seed", type=int,
                        help="deterministic order debugging/replay only; disables performance rejection")
    parser.add_argument("--case", action="append", default=[], metavar="HYPOTHESIS_ID")
    parser.add_argument("--planner-binary", type=Path)
    parser.add_argument("--planner-reference-binary", type=Path)
    parser.add_argument("--planner-bounds-workspace", type=Path)
    parser.add_argument("--physical-bit-alignment", action="store_true",
                        help="pair source-only physical alternatives across cipher revisions; preserves encryption and full work")
    parser.add_argument("--functional-defaults", action="store_true")
    parser.add_argument("--functional-command", action="append", default=[], metavar="ARGV_JSON")
    parser.add_argument("--native-coverage", type=Path)
    parser.add_argument("--python-coverage", type=Path)
    parser.add_argument("--hardware-baseline", type=Path)
    parser.add_argument("--hardware-seconds", type=float, default=5.)
    parser.add_argument("--hardware-memory-mib", type=float, default=256.)
    parser.add_argument("--skip-hardware-probes", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    args.device = args.device or ("auto" if args.profile == "gpu-dev" else "cpu")
    args.budget_seconds = args.budget_seconds if args.budget_seconds is not None else (600. if args.profile == "gpu-dev" else 300.)
    if not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 28800:
        parser.error("budget-seconds must be in (0,28800]")
    if not 1 <= args.threads <= 256 or not 1 <= args.native_threads <= 256:
        parser.error("thread counts must be in [1,256]")
    if not 0 <= args.seed <= 2**31 - 1:
        parser.error("seed must be in [0,2^31-1]")
    if args.order_seed is not None and not 0 <= args.order_seed < 2**128:
        parser.error("order-seed must be in [0,2^128)")
    if not math.isfinite(args.hardware_seconds) or not 0 < args.hardware_seconds <= 60:
        parser.error("hardware-seconds must be in (0,60]")
    if not math.isfinite(args.hardware_memory_mib) or not 1 <= args.hardware_memory_mib <= 16384:
        parser.error("hardware-memory-mib must be in [1,16384]")
    try:
        GuardPolicy(args.profile, args.suite_alpha, args.workflow_job_count)
    except ValueError as error:
        parser.error(str(error))
    if not 0 <= args.workflow_job_index < args.workflow_job_count:
        parser.error("workflow-job-index must be in [0,workflow-job-count)")
    if bool(args.planner_binary) != bool(args.planner_reference_binary):
        parser.error("native planner checks require both planner binaries")
    commands = []
    for value in args.functional_command:
        try:
            command = json.loads(value)
            if not isinstance(command, list) or not command or not all(isinstance(part, str) and part and "\x00" not in part for part in command):
                raise ValueError("expected a nonempty argv string array")
        except (ValueError, TypeError) as error:
            parser.error("invalid functional-command: " + str(error))
        commands.append(command)
    args.functional_command = commands
    return args


def run(args, *, factories=None, entry_started=None):
    """Run one declared guard job; dependency injection is for focused tests."""
    start = time.perf_counter() if entry_started is None else entry_started
    deadline = time.monotonic() + args.budget_seconds
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    adapters = []
    plan, snapshot, schedule, descriptors = None, None, {}, []
    screens, confirmations, futilities, failures, advisory, pairs = [], [], [], [], [], []
    interrupted = None
    report = {"schema": SCHEMA, "status": "running", "argv": sys.argv,
              "budget_seconds": args.budget_seconds, "profile": args.profile,
              "whole_run_boundary": "runner entry through cleanup and decision assembly; includes imports after entry, fixture preparation, workers, functional commands, hardware probes and transfers; interpreter process creation, final summary serialization/write and terminal output require an external command-wall timer",
              "ordinary_receiver_scope": "excluded", "hardware_profile": None,
              "functional_commands": [], "coverage_receipts": [], "adapter_summaries": {},
              "guard_decision": None, "complete_scope_qualification": False,
              "qualification_status": "not_evaluated_by_fast_guard"}
    write_json(output / "summary.json", report)
    try:
        if os.name != "posix":
            raise ValueError("bounded child-process cleanup requires POSIX")
        candidate = args.candidate_root.resolve(strict=True)
        reference = args.reference_root.resolve(strict=True)
        binary = args.binary.resolve(strict=True)
        reference_binary = args.reference_binary.resolve(strict=True)
        device, device_note = resolve_device(args.device)
        report["device"] = device
        if device_note:
            advisory.append(device_note)
        policy = GuardPolicy(args.profile, args.suite_alpha, args.workflow_job_count)
        for name, value in complete.child_environment(args.threads).items():
            if name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS", "PYTHONDONTWRITEBYTECODE"):
                os.environ[name] = value
        matrix = complete.read_json(ROOT / "tools/triad-performance-coverage.json")
        binaries = {"candidate": binary, "reference": reference_binary,
                    "candidate_planner": args.planner_binary, "reference_planner": args.planner_reference_binary}
        snapshot = source_snapshot(candidate, reference, binaries)
        context = {"candidate_root": candidate, "reference_root": reference, "binary": binary,
            "reference_binary": reference_binary, "fixture_cache": (args.fixture_cache or output / "fixtures").resolve(),
            "device": device, "precision": args.precision, "threads": args.threads,
            "native_threads": args.native_threads, "deadline": deadline,
            "physical_bit_alignment": args.physical_bit_alignment,
            "planner_binary": args.planner_binary.resolve() if args.planner_binary else None,
            "planner_reference_binary": args.planner_reference_binary.resolve() if args.planner_reference_binary else None,
            "planner_bounds_workspace": args.planner_bounds_workspace.resolve() if args.planner_bounds_workspace else None}
        for name, factory in (adapter_factories() if factories is None else factories):
            adapter = factory({**context, "output": output / "adapters" / name})
            adapters.append((name, adapter))
        descriptors, owners = collect_descriptors(adapters, args.case)
        ids = [row["id"] for row in descriptors]
        # Family/job multiplicity can increase exact-test look sizes. Resolve
        # those counts before freezing every invocation and its random order.
        preliminary = build_guard_plan(ids, policy)
        schedule = measurement_schedule(ids, policy, seed=args.seed, session=uuid.uuid4().hex,
                                        looks=preliminary["looks"], order_seed=args.order_seed)
        plan = build_guard_plan(ids, policy, metadata={
            "descriptors": descriptors, "measurement_schedule": schedule,
            "source_and_build_identity": snapshot, "device": device, "precision": args.precision,
            "threads": args.threads, "native_threads": args.native_threads,
            "workflow_job_index": args.workflow_job_index,
            "case_selection": "explicit diagnostic subset" if args.case else "all declared applicable workload families",
            "screen_policy": "all available screens precede every confirmation; screen times never enter confirmation",
            "order_randomization": {"method": "fresh OS random coin per block" if args.order_seed is None else "deterministic diagnostic order replay",
                                    "order_seed": args.order_seed, "uses_timing_data": False,
                                    "independence_guaranteed_by_random_order": False,
                                    "experimental_unit": "one balanced four-arm block; two complete targets per source"},
            "randomization_inference_eligible": args.order_seed is None,
            "confirmation_budget_policy": "cheap-first round-robin nominees; admit one complete block using 1.25x latest block wall (first block 2.5x screen pair wall); setup/warmup/cleanup included in effort; fixed rejection looks unchanged",
            "early_futility_policy": "stop only when the exact policy proves no remaining predeclared look can reject even if all future signs are positive",
            "functional_defaults_requested": args.functional_defaults,
            "hardware_probe_seconds": args.hardware_seconds,
            "hardware_probes_advisory_only": True})
        write_json(output / "plan.json", plan)
        report.update(plan_sha256=plan["record_sha256"], hypothesis_count=len(ids),
                      suite_alpha=policy.suite_alpha, workflow_job_count=policy.workflow_job_count,
                      device=device, descriptors=descriptors, source_identity=snapshot)
        if args.case:
            advisory.append("explicit --case subset: this guard does not cover the remaining maintained workload family")
        coverage, coverage_advisory = check_coverage_receipts(args, candidate, matrix)
        report["coverage_receipts"] = coverage
        advisory.extend(coverage_advisory)
        functional, functional_failures, functional_advisory = run_functional(
            args, candidate, binary, matrix, output, deadline, device)
        report["functional_commands"] = functional
        failures.extend(functional_failures)
        advisory.extend(functional_advisory)
        screens, confirmations, futilities, work_failures, work_advisory, pairs = exercise_workloads(
            adapters, descriptors, owners, plan, schedule, deadline=deadline, output=output / "measurements")
        failures.extend(work_failures)
        advisory.extend(work_advisory)
        if not args.skip_hardware_probes and time.monotonic() < deadline:
            from triad_perf_hardware import collect_profile
            try:
                hardware = collect_profile(device=device, threads=args.threads, seconds=args.hardware_seconds,
                    memory_limit_bytes=int(args.hardware_memory_mib * 1024**2), deadline=deadline)
                report["hardware_profile"] = hardware
                write_json(output / "hardware-profile.json", hardware)
                report["hardware_health"] = _legacy_api().hardware_health(hardware, args.hardware_baseline)
                if hardware["status"] != "complete" or report["hardware_health"].get("warning") or report["hardware_health"]["status"] != "raw_hardware_screen":
                    advisory.append("hardware capability/health screen incomplete, unavailable or warned; diagnostic only")
            except Exception as error:
                report["hardware_health"] = {"status": "unavailable", "error": type(error).__name__ + ": " + str(error), "blocking": False}
                advisory.append("hardware probe unavailable: " + str(error))
        else:
            report["hardware_health"] = {"status": "not_run", "reason": "requested skip or shared budget exhausted", "blocking": False}
            advisory.append("hardware probes were not run; no hardware-health coverage inferred")
    except (KeyboardInterrupt, SystemExit) as error:
        interrupted = error
        failures.append("guard execution was interrupted; completed qualification or guard coverage is not claimed")
        report["execution_error"] = {"type": type(error).__name__, "message": str(error), "interrupted": True}
    except Exception as error:
        failures.append(type(error).__name__ + ": " + str(error))
        report["execution_error"] = {"type": type(error).__name__, "message": str(error), "traceback": traceback.format_exc()}
    finally:
        for name, adapter in reversed(adapters):
            try:
                adapter.close()
                summary = adapter.summary() if callable(getattr(adapter, "summary", None)) else {"status": "closed"}
                report["adapter_summaries"][name] = summary
            except Exception as error:
                failures.append(name + ": adapter cleanup/integrity failure: " + type(error).__name__ + ": " + str(error))
                report["adapter_summaries"][name] = {"status": "cleanup_failed", "error": str(error)}
        if snapshot is not None:
            try:
                report["source_integrity"] = verify_snapshot(snapshot)
                if report["source_integrity"]["status"] != "passed":
                    failures.append("source/build bytes changed during guard: " + ", ".join(report["source_integrity"]["changed"]))
            except Exception as error:
                failures.append("source/build final identity check failed: " + str(error))
                report["source_integrity"] = {"status": "failed", "error": str(error)}
        if plan is not None:
            try:
                decision = build_guard_report(plan, screens, confirmations, functional_failures=failures,
                                              advisory=advisory, futility_records=futilities)
                report["guard_decision"] = decision
                for field in ("status", "exit_code", "performance_status", "functional_status",
                              "functional_failures", "confirmed_regressions", "qualification_status"):
                    report[field] = decision[field]
                limitations = list(decision["advisory"])
            except Exception as error:
                failures.append("guard evidence replay failed: " + str(error))
                report.update(status="blocked", exit_code=2, functional_failures=failures,
                              confirmed_regressions=[], performance_status="unavailable")
                limitations = advisory + ["guard evidence failed integrity replay"]
        else:
            report.update(status="blocked", exit_code=2, functional_failures=failures,
                          confirmed_regressions=[], performance_status="unavailable")
            limitations = advisory + ["setup failed before a complete hypothesis family could be frozen"]
        report.update(assurance_limitations=limitations, limited_assurance=bool(limitations),
            affirmative_non_regression_certification=False, complete_scope_qualification=False,
            measurement_count=len(pairs), measurements=pairs,
            outcomes={"performance": report["performance_status"],
                      "functional_or_integrity": "failed" if report["functional_failures"] else "no_failure_reported",
                      "qualification": "not_evaluated_by_fast_guard",
                      "coverage_assurance": "limited" if limitations else "declared_guard_scope_only"},
            budget_exhausted=time.monotonic() >= deadline,
            wall_seconds=time.perf_counter() - start,
            entry_to_report_serialization_seconds=time.perf_counter() - ENTRY_STARTED)
        # The returned object has the same exact JSON types as the retained
        # receipt, including adapter tuples and numerical scalar metadata.
        report = json.loads(json.dumps(report, allow_nan=False, default=_json_default))
        write_json(output / "summary.json", report)
    if interrupted is not None:
        raise interrupted
    return report


def main(argv=None):
    args = parse_args(argv)
    previous = signal.getsignal(signal.SIGTERM)
    def terminate(signum, frame):
        raise KeyboardInterrupt("received termination signal")
    signal.signal(signal.SIGTERM, terminate)
    try:
        report = run(args, entry_started=ENTRY_STARTED)
    finally:
        signal.signal(signal.SIGTERM, previous)
    print(json.dumps({key: report.get(key) for key in ("status", "exit_code", "profile", "device",
        "performance_status", "confirmed_regressions", "functional_failures", "limited_assurance",
        "qualification_status", "wall_seconds")}, allow_nan=False))
    return report["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
