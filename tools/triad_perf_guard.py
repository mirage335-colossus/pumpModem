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
import signal
import sys
import traceback
import uuid

import triad_perf_complete as complete
from triad_perf_guard_policy import (GuardPolicy, PairObservation, build_guard_plan,
                                     build_guard_report, confirm_case, confirmation_futility,
                                     screen_case)


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-performance-fast-guard-run-v1"


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
    names = {"CMakeLists.txt"}
    for row in matrix["rows"]:
        names.update(row["source_files"])
        names.update(test.split("::", 1)[0] for test in row["cheap_functional_tests"]
                     if test.startswith(("tests/", "tools/")))
    for case in families["cases"]:
        names.update("tools/" + name for name in case["dependencies"])
    paths, missing = {}, {}
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
                absent.append(name)
        missing[label] = absent
    harness = set((ROOT / "tools").glob("triad_perf_guard*.py"))
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
            "missing_declared_historical_files": missing,
            "scope": "current files bound before/after; loaded worker dependencies and full-work checks are additionally verified by each adapter"}


def verify_snapshot(snapshot):
    changed = []
    for name, filename in snapshot["paths"].items():
        path = Path(filename)
        if not path.is_file() or _sha(path) != snapshot["sha256"][name]:
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


def measurement_schedule(ids, policy, *, seed, session, looks=None):
    """Freeze orders and nonoverlapping multi-capture seeds before screening."""
    looks = policy.looks if looks is None else tuple(looks)
    if not looks or any(type(n) is not int or n < 1 for n in looks):
        raise ValueError("measurement schedule needs positive frozen look counts")
    generator = random.Random(seed)
    result = {}
    for case_index, name in enumerate(ids):
        def order():
            return ["reference", "candidate"] if generator.getrandbits(1) == 0 else ["candidate", "reference"]
        result[name] = {"screen": {"run_id": session + ":screen:" + str(case_index),
            "seed": seed, "order": order()}, "confirmation": [
                {"run_id": session + ":confirmation:" + str(case_index) + ":" + str(index),
                 "seed": seed + 1_000_000 + 16 * (case_index * max(looks) + index),
                 "order": order()} for index in range(max(looks))]}
    return result


def _observation(raw, expected_id):
    if not isinstance(raw, dict) or raw.get("run_id") != expected_id:
        raise ValueError("adapter returned a missing or different outer run identity")
    if not isinstance(raw.get("details"), dict):
        raise ValueError("adapter omitted work/integrity evidence")
    return PairObservation(raw["run_id"], raw["reference_seconds"], raw["candidate_seconds"])


def exercise_workloads(adapters, descriptors, owners, plan, schedule, *, deadline, output,
                       clock=time.monotonic):
    """All screens precede any independent confirmation, without rerun-to-pass."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    screens, confirmations, futilities, failures, advisory, measurements = [], [], [], [], [], []
    disabled = set()
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
            # Serialize before accepting the evidence: nonfinite work metadata
            # is an integrity error even if the timer itself is finite.
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
            write_json(output / ("pair-%04d.json" % (len(measurements) - 1)), receipt)
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
            value = screen_case(plan, name, pair)
            screens.append(value)
            # A shared-generation screen can be cheaper than its composed
            # pipeline timer. Use both observed costs as planning diagnostics.
            screen_wall[name] = max(receipt["wall_seconds"], pair.reference_seconds + pair.candidate_seconds)
    screen_by_id = {row["hypothesis_id"]: row for row in screens}
    for descriptor in descriptors:
        name = descriptor["id"]
        screen = screen_by_id.get(name)
        if not screen or not screen["nominate_confirmation"] or id(owners[name]) in disabled:
            continue
        rows = []
        finished = False
        for look in plan["looks"]:
            needed = look - len(rows)
            estimated = 1.25 * needed * screen_wall[name]
            remaining = deadline - clock()
            if remaining <= 0 or estimated > remaining:
                advisory.append(name + ": next predeclared confirmation look not started; estimated %.3fs, remaining %.3fs" % (estimated, max(0., remaining)))
                break
            failed = False
            for index in range(len(rows), look):
                if clock() >= deadline:
                    advisory.append(name + ": confirmation sequence interrupted by shared budget")
                    failed = True
                    break
                pair, _ = measure(name, schedule[name]["confirmation"][index], "confirmation")
                if pair is None:
                    failed = True
                    break
                rows.append(pair)
                # Evaluate only a predeclared rejection look. Between looks,
                # stop only when the exact policy proves that even every
                # remaining sign being positive could never cause rejection.
                if len(rows) in plan["looks"]:
                    decision = confirm_case(plan, name, rows, screen_run_ids=[screen["observation"]["run_id"]])
                    confirmations.append(decision)
                    if decision["confirmed_regression"]:
                        if profile == "ci":
                            advisory.append("CI stopped further confirmations after the first confirmed regression; all available screens had already run")
                            return screens, confirmations, futilities, failures, advisory, measurements
                        finished = True
                        break
                if len(rows) < plan["looks"][-1]:
                    futility = confirmation_futility(plan, name, rows,
                        screen_run_ids=[screen["observation"]["run_id"]])
                    if futility["futile"]:
                        futilities.append(futility)
                        write_json(output / ("futility-%04d.json" % (len(futilities) - 1)), futility)
                        finished = True
                        break
            if failed or finished:
                break
    return screens, confirmations, futilities, failures, advisory, measurements


def _required_methods(matrix, command):
    filename = next((Path(arg).name for arg in command if arg.endswith(".py")), None)
    if filename is None:
        return []
    return sorted({test.split("::", 1)[1] for row in matrix["rows"]
                   for test in row["cheap_functional_tests"]
                   if test.startswith("tests/" + filename + "::")})


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
    parser.add_argument("--case", action="append", default=[], metavar="HYPOTHESIS_ID")
    parser.add_argument("--planner-binary", type=Path)
    parser.add_argument("--planner-reference-binary", type=Path)
    parser.add_argument("--planner-bounds-workspace", type=Path)
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
                                        looks=preliminary["looks"])
        plan = build_guard_plan(ids, policy, metadata={
            "descriptors": descriptors, "measurement_schedule": schedule,
            "source_and_build_identity": snapshot, "device": device, "precision": args.precision,
            "threads": args.threads, "native_threads": args.native_threads,
            "workflow_job_index": args.workflow_job_index,
            "case_selection": "explicit diagnostic subset" if args.case else "all declared applicable workload families",
            "screen_policy": "all available screens precede every confirmation; screen times never enter confirmation",
            "order_randomization": {"method": "predeclared seeded pseudorandom AB/BA draws",
                                    "seed": args.seed, "uses_timing_data": False,
                                    "independence_guaranteed_by_random_order": False},
            "confirmation_budget_policy": "next full look starts only when a 1.25x measured-cost planning estimate fits; not a statistical limit",
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
