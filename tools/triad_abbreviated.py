#!/usr/bin/env python3
"""Measure abbreviated performance of the compiled Robust receiver.

Compare against a matched native report for a relative regression decision.
The manual case entry point evaluates absolute complete-receiver capacity.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import statistics
import sys

from triad_case_metrics import BATCHES, LOOKS, log_interval, t_critical
from triad_cases import DEFAULT_CASES
from triad_native import (BACKENDS, DEFAULT_PROBE, NativeProbe, ProbeError, allocation,
                          environment, finite_number, is_sha256, named_phases, positive_int,
                          read_report, runtime_identity, validate_receipt, write_result)

STRATA = ("active", "noise")
PHASES = ("native_slot", "coarse", "fine", "audio_frontend")
KERNELS = tuple(f"{stratum}_{phase}" for stratum in STRATA for phase in PHASES)
ALPHA = .05
PRECISION = .15
MODEL_LIMIT = .30
SLOWDOWN = 1.30
SCHEMA = "datapump.triad.native-regression.v1"


def analyze(rows, target_units, *, baseline=None):
    """Analyze a declared look; each row is one outer trial of all three sizes."""
    if len(rows) not in LOOKS:
        raise ValueError("observations must end at a predeclared look")
    if isinstance(target_units, bool) or not isinstance(target_units, int) or not 4 <= target_units <= 10**9:
        raise ValueError("target units must be an integer in [4,1000000000]")
    if any(len(row) != 3 or any(type(x) not in (int, float) or not math.isfinite(x) or x <= 0
                               for x in row) for row in rows):
        raise ValueError("each outer trial needs three positive batch timings")
    # Reserve all candidate/reference look pairs as well as three claim kinds.
    # A baseline may itself have stopped early; allocation covers that selection.
    # Unmade claims and early stopping never reclaim their reserved alpha.
    alpha = ALPHA/(len(KERNELS)*len(LOOKS)**2*3)
    units = [(row[0] + row[1])/3 for row in rows]
    errors = [row[2]/(4*unit) for row, unit in zip(rows, units)]
    point, interval = log_interval(units, alpha=alpha)
    _, error_interval = log_interval(errors, alpha=alpha)
    # Observed lack-of-fit is a nonshrinking floor, not a confidence bound on
    # arbitrary future regimes. Repetition cannot average this allowance away.
    error_floor = max(abs(math.log(x)) for x in errors)
    allowance = max(error_floor, *(abs(math.log(x)) for x in error_interval))
    model_supported = (error_interval[0] >= 1/(1+MODEL_LIMIT)
                       and error_interval[1] <= 1+MODEL_LIMIT
                       and error_floor <= math.log1p(MODEL_LIMIT))
    relative_width = max(interval[1]/point-1, 1-interval[0]/point)
    decision = "uncompared"
    ratio = None
    if baseline is not None:
        baseline_floor = baseline["observed_model_log_error_floor"]
        if (isinstance(baseline_floor, bool) or
            not isinstance(baseline_floor, (int, float)) or
            not math.isfinite(baseline_floor) or baseline_floor < 0):
            raise ValueError("baseline model error floor must be finite and nonnegative")
        if baseline["model_status"] not in ("abbreviated_holdout_supported", "unresolved"):
            raise ValueError("invalid baseline model status")
        reference = baseline["training_unit_seconds"]
        if (len(reference) not in LOOKS or
            any(not math.isfinite(x) or x <= 0 for x in reference)):
            raise ValueError("baseline requires finite positive outer observations at a declared look")
        # This is an independent-run comparison, not fictitious paired data.
        a = [math.log(x) for x in units]
        b = [math.log(x) for x in reference]
        if len(b) not in LOOKS or any(not math.isfinite(x) for x in b):
            raise ValueError("invalid baseline outer observations")
        va, vb = statistics.variance(a)/len(a), statistics.variance(b)/len(b)
        variance = va+vb
        degrees = (variance*variance/(va*va/(len(a)-1)+vb*vb/(len(b)-1))
                   if variance else len(a)+len(b)-2)
        half = t_critical(alpha, degrees)*math.sqrt(variance)
        center = statistics.fmean(a)-statistics.fmean(b)
        floor = error_floor + baseline_floor
        ratio = [math.exp(center-half-floor), math.exp(center+half+floor)]
        if not model_supported or baseline["model_status"] != "abbreviated_holdout_supported":
            decision = "unresolved_model"
        elif ratio[0] > SLOWDOWN:
            decision = "material_slowdown"
        elif ratio[1] < SLOWDOWN:
            decision = "no_material_slowdown_for_measured_kernels"
        else:
            decision = "inconclusive"
    resolved = (decision in ("material_slowdown", "no_material_slowdown_for_measured_kernels")
                if baseline is not None else model_supported and relative_width <= PRECISION)
    return {
        "outer_trials": len(rows), "batch_seconds": rows,
        "training_unit_seconds": units,
        "measured_kernel_units": sum(BATCHES)*len(rows),
        "measured_seconds": sum(sum(row) for row in rows),
        "unit_geometric_mean_seconds": point, "unit_geometric_mean_interval_seconds": interval,
        "holdout_ratio_interval": error_interval,
        "observed_model_log_error_floor": error_floor,
        "model_status": "abbreviated_holdout_supported" if model_supported else "unresolved",
        "projected_target_units": target_units,
        "projected_seconds": point*target_units if model_supported else None,
        "conditional_projection_interval_seconds":
            [target_units*interval[0]*math.exp(-allowance), target_units*interval[1]*math.exp(allowance)]
            if model_supported else None,
        "comparison": decision, "candidate_over_baseline_interval": ratio,
        "stop": resolved or len(rows) == LOOKS[-1],
        "stop_reason": "decision_or_precision" if resolved else "bounded_limit" if len(rows) == LOOKS[-1] else "next_abbreviated_look",
    }


def policy():
    return {
        "looks": list(LOOKS), "batch_units": list(BATCHES), "strata": list(STRATA),
        "phases": list(PHASES), "family_alpha": ALPHA,
        "claims_per_phase_look_pair": 3, "relative_precision": PRECISION,
        "maximum_holdout_error": MODEL_LIMIT, "material_slowdown_ratio": SLOWDOWN,
        "model": "Additive cost of identical complete native slot and audio frontend units",
        "warmup_and_setup": "The native receipt identifies preparation and the measured phase boundary",
        "projection_limit": "Only repetitions of measured geometry and work",
        "confidence_limit": "Conditional log-mean intervals with observed model-error allowance",
        "excluded_claims": ["communication sensitivity", "absolute real-time capacity"],
    }


def validate_baseline(baseline, geometry, host):
    if (not isinstance(baseline, dict) or baseline.get("schema") != SCHEMA
            or baseline.get("geometry") != geometry or baseline.get("environment") != host):
        raise ProbeError("Baseline must match the native schema, observation, geometry, allocation and environment")
    if baseline.get("policy") != policy() or set(baseline.get("kernels", {})) != set(KERNELS):
        raise ProbeError("Baseline must use the same inference policy and complete phase coverage")


def _unit_phases(receipt, name, count):
    phases = named_phases(receipt, name)
    if len(phases) != count:
        raise ProbeError(f"Native regression returned {len(phases)} {name} phases for {count} units")
    ordered = sorted(phases, key=lambda phase: phase["unit_index"])
    if any(type(phase["unit_index"]) is not int for phase in ordered) or [
            phase["unit_index"] for phase in ordered] != list(range(count)):
        raise ProbeError(f"Native regression returned invalid unit indices for {name}")
    return ordered


def regression_batch(receipt, stratum, count):
    """Return observed native costs and the fixed unit identity for one batch."""
    if receipt["status"] != "complete":
        raise ProbeError(receipt.get("reason") or "Native regression work did not complete")
    slots = _unit_phases(receipt, "native_slot", count)
    frontend = _unit_phases(receipt, "audio_frontend", count)
    observation = receipt["observation"]
    if not is_sha256(observation.get("input_sha256")):
        raise ProbeError("Native regression is missing the fixed IQ observation identity")
    expected = "quiet" if stratum == "noise" else "data"
    work = [phase["work"] for phase in slots]
    if (receipt["result"].get("units") != count or receipt["result"].get("stratum") != stratum
            or receipt["result"].get("work_complete") is not True
            or receipt["result"].get("evaluator_expected_decision_observed") is not True):
        raise ProbeError("Native regression receipt did not finish its declared units")
    if any(not is_sha256(item.get("work_identity_sha256"))
           or item.get("decision") != expected or item.get("complete") is not True
           for item in work):
        raise ProbeError(f"Native regression did not execute the declared {expected} decision work")
    ids = {item["work_identity_sha256"] for item in work}
    if len(ids) != 1:
        raise ProbeError("Native repeated-unit work changed inside a regression batch")
    pcm = frontend[0]["work"]
    if (pcm.get("input_rows") != observation.get("samples") or pcm.get("output_rows", 0) <= 0
            or any(phase["work"] != pcm for phase in frontend)):
        raise ProbeError("Native regression PCM frontend work is incomplete or changed")
    times = {
        "native_slot": sum(phase["wall_seconds"] for phase in slots),
        "coarse": sum(finite_number(item.get("coarse_seconds"), "coarse wall time", nonnegative=True)
                      for item in work),
        "fine": sum(finite_number(item.get("fine_seconds"), "fine wall time", nonnegative=True)
                    for item in work),
        "audio_frontend": sum(phase["wall_seconds"] for phase in frontend),
    }
    if any(value <= 0 for value in times.values()):
        raise ProbeError("A declared native regression phase has no measured work")
    geometry = {"configuration": receipt["configuration"], "allocation": allocation(receipt),
                "observation": observation, "work_identity_sha256": ids.pop(), "audio_frontend": pcm}
    return times, geometry


def _baseline_kernel(baseline, name, target_units):
    row = baseline["kernels"][name]
    # Re-evaluate the raw look using the current policy before using its evidence.
    measured = analyze(row["batch_seconds"], target_units)
    for key in ("training_unit_seconds", "observed_model_log_error_floor", "model_status"):
        if row.get(key) != measured[key]:
            raise ProbeError(f"Baseline has inconsistent {name} inference evidence")
    return measured


def _comparison_environment(backend, description):
    return environment(backend) | {"allocation": allocation(description),
                                    "native_runtime": runtime_identity(description)}


def _validate_baseline_evidence(baseline, host, backend, target_units):
    """Bind each reference batch to the reported native executable and its unit cost."""
    if baseline.get("identity_verified_at_completion") is not True:
        raise ProbeError("Baseline was not finalized with unchanged native identity")
    desc = validate_receipt(baseline.get("description"), command="describe", backend=backend, returncode=0)
    if (desc["identity"] != baseline.get("native_identity")
            or not is_sha256(desc["identity"].get("executable_sha256"))
            or _comparison_environment(backend, desc) != host):
        raise ProbeError("Baseline native executable or runtime identity is inapplicable")
    batches = baseline.get("batches")
    if not isinstance(batches, dict) or set(batches) != set(STRATA):
        raise ProbeError("Baseline lacks complete native batch evidence")
    for stratum, batch in batches.items():
        rows = {}
        for sample in batch:
            index = positive_int(sample["look_row"], "baseline look row")
            units = positive_int(sample["units"], "baseline batch units")
            sizes = rows.setdefault(index, {})
            if units not in BATCHES or units in sizes:
                raise ProbeError("Baseline repeated or undeclared native unit batch")
            receipt = validate_receipt(sample["receipt"], command="regression", backend=backend, returncode=0)
            if (any(receipt["identity"].get(key) != desc["identity"][key]
                    for key in ("source_manifest_sha256", "executable_sha256"))
                    or _comparison_environment(backend, receipt) != host):
                raise ProbeError("Baseline native batch identity changed")
            timings, geometry = regression_batch(receipt, stratum, units)
            if geometry != baseline["geometry"][stratum]:
                raise ProbeError("Baseline native observation or work differs from its declared geometry")
            sizes[units] = timings
        if len(rows) not in LOOKS or sorted(rows) != list(range(1, len(rows) + 1)):
            raise ProbeError("Baseline lacks complete declared outer looks")
        for phase in PHASES:
            name = f"{stratum}_{phase}"
            observed = [[rows[index][units][phase] for units in BATCHES] for index in range(1, len(rows) + 1)]
            if observed != _baseline_kernel(baseline, name, target_units)["batch_seconds"]:
                raise ProbeError("Baseline timing summaries do not match native phase observations")


def run(*, target_units=1000, device="cpu", baseline=None, executable=DEFAULT_PROBE,
        workers=None, slot_owners=None, memory_limit_bytes=None, deadline_ms=30_000,
        max_samples=4_194_304, tensor_precision=None, device_index=None, device_memory_limit_bytes=None):
    probe = NativeProbe(executable, backend=device)
    if baseline is not None and not isinstance(baseline, dict):
        raise ProbeError("Baseline report must be a JSON object")
    # Freeze the cap across both reports when the operator has left it automatic.
    if baseline is not None and memory_limit_bytes is None:
        baseline_environment = baseline.get("environment")
        if not isinstance(baseline_environment, dict) or not isinstance(baseline_environment.get("allocation"), dict):
            raise ProbeError("Baseline lacks native resource allocation")
        memory_limit_bytes = baseline_environment["allocation"].get("memory_limit_bytes")
    options = {"workers": workers, "slot_owners": slot_owners, "memory_limit_bytes": memory_limit_bytes,
               "deadline_ms": deadline_ms, "max_samples": max_samples,
               "tensor_precision": tensor_precision, "device_index": device_index,
               "device_memory_limit_bytes": device_memory_limit_bytes}
    description = probe.call("describe", **options)
    report = {"schema": SCHEMA, "policy": policy(), "native_identity": description["identity"],
              "description": description, "geometry": {}, "kernels": {}, "batches": {},
              "baseline_identity": baseline.get("native_identity") if baseline else None}
    if description["status"] != "complete":
        return report | {"status": description["status"], "reason": description.get("reason")}
    host = _comparison_environment(device, description)
    report["environment"] = host
    options["memory_limit_bytes"] = host["allocation"]["memory_limit_bytes"]
    if baseline is not None:
        validate_baseline(baseline, baseline.get("geometry"), host)
        if set(baseline["geometry"]) != set(STRATA):
            raise ProbeError("Baseline is missing a native workload stratum")
        _validate_baseline_evidence(baseline, host, device, target_units)
    for stratum in STRATA:
        rows = {phase: [] for phase in PHASES}
        receipts = report["batches"].setdefault(stratum, [])
        for look in LOOKS:
            while len(rows["native_slot"]) < look:
                row = {phase: [None] * len(BATCHES) for phase in PHASES}
                offset = len(rows["native_slot"])
                for index in ((offset + turn) % 3 for turn in range(3)):
                    units = BATCHES[index]
                    receipt = probe.call("regression", stratum=stratum, units=units, **options)
                    receipts.append({"look_row": offset + 1, "units": units, "receipt": receipt})
                    if receipt["status"] != "complete":
                        return report | {"status": receipt["status"], "reason": receipt.get("reason")}
                    if (runtime_identity(receipt) != host["native_runtime"] or allocation(receipt) != host["allocation"]
                            or receipt["identity"]["source_manifest_sha256"] != description["identity"]["source_manifest_sha256"]):
                        raise ProbeError("Native regression runtime or allocation changed between batches")
                    times, geometry = regression_batch(receipt, stratum, units)
                    if report["geometry"].setdefault(stratum, geometry) != geometry:
                        raise ProbeError("Native regression observation, configuration or work changed between batches")
                    if baseline is not None and baseline["geometry"][stratum] != geometry:
                        raise ProbeError("Baseline native observation, geometry or work is inapplicable")
                    for phase, seconds in times.items():
                        row[phase][index] = seconds
                for phase, seconds in row.items():
                    rows[phase].append(seconds)
            for phase, measured in rows.items():
                name = f"{stratum}_{phase}"
                reference = _baseline_kernel(baseline, name, target_units) if baseline else None
                report["kernels"][name] = analyze(measured, target_units, baseline=reference)
            if all(report["kernels"][f"{stratum}_{phase}"]["stop"] for phase in PHASES):
                break
    probe.verify_unchanged()
    final_environment = environment(device)
    if final_environment != {key: host[key] for key in final_environment}:
        raise ProbeError("Host environment changed during native regression")
    report["identity_verified_at_completion"] = True
    results = report["kernels"].values()
    comparisons = [result["comparison"] for result in results]
    if baseline is None:
        report["status"] = ("baseline_established" if all(
            result["model_status"] == "abbreviated_holdout_supported" for result in results)
            else "baseline_evidence_unresolved")
    elif "material_slowdown" in comparisons:
        report["status"] = "material_slowdown"
    elif all(value == "no_material_slowdown_for_measured_kernels" for value in comparisons):
        report["status"] = "no_material_slowdown_for_measured_kernels"
    else:
        report["status"] = "inconclusive"
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--device", choices=BACKENDS, default="cpu")
    parser.add_argument("--tensor-precision", choices=("automatic", "fp64"))
    parser.add_argument("--device-index", type=int)
    parser.add_argument("--device-workspace-mib", type=int)
    parser.add_argument("--target-units", type=int, default=1000, help="Projection only. Executes bounded 1/2/4 batches.")
    parser.add_argument("--cases", nargs="?", const=str(DEFAULT_CASES), help="Use the manual capacity entry point.")
    parser.add_argument("--probe", "--exporter", type=Path, dest="probe", default=DEFAULT_PROBE,
                        help="Compiled receiver probe. The legacy exporter option selects the sibling probe.")
    parser.add_argument("--probe-seconds", type=float, default=1.)
    parser.add_argument("--threads", type=int, help="Exact worker override. Omitted uses 2H.")
    parser.add_argument("--slot-owners", type=int, help="Exact native slot owner limit.")
    parser.add_argument("--workspace-mib", type=int, help="Exact native host memory limit.")
    parser.add_argument("--deadline-ms", type=int, default=30_000, help="Deadline per bounded native call.")
    parser.add_argument("--max-samples", type=int, default=4_194_304, help="Generated sample cap per native call.")
    parser.add_argument("--case-index", type=int, action="append")
    parser.add_argument("--skip-message-gate", action="store_true")
    input_args = list(sys.argv[1:] if argv is None else argv)
    args = parser.parse_args(input_args)
    if args.cases is not None:
        explicit = {arg.split("=", 1)[0] for arg in input_args}
        if explicit & {"--baseline", "--device", "--target-units", "--tensor-precision",
                       "--device-index", "--device-workspace-mib"}:
            parser.error("Relative regression options do not apply to manual capacity")
        from triad_manual_cases import main as manual_main
        manual_args = ["--case-list", args.cases, "--output", str(args.output),
                       "--probe" if "--exporter" not in explicit else "--exporter", str(args.probe),
                       "--measurement-seconds", str(args.probe_seconds)]
        for option, value in (("--threads", args.threads), ("--slot-owners", args.slot_owners),
                              ("--workspace-mib", args.workspace_mib), ("--deadline-ms", args.deadline_ms),
                              ("--max-samples", args.max_samples)):
            if option in explicit:
                manual_args.extend((option, str(value)))
        for index in args.case_index or ():
            manual_args.extend(("--case-index", str(index)))
        if args.skip_message_gate:
            manual_args.append("--skip-message-gate")
        return manual_main(manual_args)
    if args.case_index is not None or args.skip_message_gate:
        parser.error("--case-index and --skip-message-gate require --cases")
    if not 4 <= args.target_units <= 10**9:
        parser.error("--target-units must be in [4,1000000000]")
    if args.device == "cpu" and any(value is not None for value in (
            args.tensor_precision, args.device_index, args.device_workspace_mib)):
        parser.error("Tensor options require --device tensor-cpu or cuda")
    try:
        options = {"target_units": args.target_units, "device": args.device,
                   "baseline": read_report(args.baseline) if args.baseline else None}
        if args.probe != DEFAULT_PROBE:
            options["executable"] = args.probe
        if args.threads is not None:
            options["workers"] = positive_int(args.threads, "threads")
        if args.slot_owners is not None:
            options["slot_owners"] = positive_int(args.slot_owners, "slot owners")
        if args.workspace_mib is not None:
            options["memory_limit_bytes"] = positive_int(args.workspace_mib, "workspace MiB") * 2**20
        if args.deadline_ms != 30_000:
            options["deadline_ms"] = positive_int(args.deadline_ms, "deadline")
        if args.max_samples != 4_194_304:
            options["max_samples"] = positive_int(args.max_samples, "sample limit")
        if args.tensor_precision is not None:
            options["tensor_precision"] = args.tensor_precision
        if args.device_index is not None:
            options["device_index"] = positive_int(args.device_index, "device index", allow_zero=True)
        if args.device_workspace_mib is not None:
            options["device_memory_limit_bytes"] = positive_int(
                args.device_workspace_mib, "device workspace MiB") * 2**20
        result = run(**options)
    except (ValueError, OSError, KeyError, TypeError) as error:
        result = {"schema": SCHEMA, "status": "error", "reason": str(error)}
    write_result(args.output, result)
    print(f"{result['status']}: {args.output}")
    if result.get("reason"):
        print(result["reason"], file=sys.stderr)
    if result["status"] in ("baseline_established", "no_material_slowdown_for_measured_kernels"):
        return 0
    return 1 if result["status"] in ("material_slowdown", "error") else 2


if __name__ == "__main__":
    sys.exit(main())
