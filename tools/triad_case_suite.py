"""Manual case selection, native calibration orchestration and reports."""
from __future__ import annotations

import copy
from pathlib import Path
import time

from triad_case_capacity import (INTERVAL, RATIO, measure_complete_case, measurement_policy,
                                 unqualified_blind_capacity, worker_plan)
from triad_case_metrics import success_interval
from triad_cases import DEFAULT_CASES, load_cases
from triad_cpu import detect_cpu_topology
from triad_native import (NativeProbe, ProbeError, allocation, environment, finite_number,
                          positive_int, runtime_identity, sha256_file, write_result)

SCHEMA = "datapump.triad.native-capacity.v1"
SENSITIVITY_SCHEMA = "datapump.triad.native-sensitivity.v1"


def select_cases(parsed, indices=None):
    """Use freshly parsed row indices and preserve the source's order."""
    if indices is None:
        return parsed["cases"]
    wanted = set(indices)
    available = {case["index"] for case in parsed["cases"]}
    if not wanted or any(type(index) is not int for index in wanted) or not wanted <= available:
        raise ProbeError("Selected case indices must exist in the freshly parsed list")
    return [case for case in parsed["cases"] if case["index"] in wanted]


def _limits(deadline_ms, row_deadline_ms, max_samples, coarse_tiles, workspace_mib, slot_owners):
    for name, value in (("deadline", deadline_ms), ("row deadline", row_deadline_ms),
                        ("sample cap", max_samples), ("coarse tile cap", coarse_tiles)):
        positive_int(value, name)
    if deadline_ms > 3_600_000 or row_deadline_ms > 3_600_000 or coarse_tiles > 1_048_576:
        raise ProbeError("Manual case time or tile cap exceeds its declared bound")
    if slot_owners is not None:
        positive_int(slot_owners, "slot owners")
    memory = None if workspace_mib is None else positive_int(workspace_mib, "workspace MiB") * 2**20
    return {"deadline_ms": deadline_ms, "max_samples": max_samples, "coarse_tiles": coarse_tiles,
            "memory_limit_bytes": memory, "slot_owners": slot_owners}


def _verify_final(report, probe, parsed, host):
    try:
        probe.verify_unchanged()
        unchanged = sha256_file(parsed["source_path"]) == parsed["source_sha256"]
        unchanged &= environment(probe.backend) == host
        report["identity_verified_at_completion"] = unchanged
    except (ValueError, OSError) as error:
        report["identity_verified_at_completion"] = False
        report["identity_error"] = str(error)
    if not report["identity_verified_at_completion"]:
        report["status"] = "invalidated_source_or_configuration_changed"


def _format(value):
    if value is None:
        return "unresolved"
    point = value.get(RATIO)
    if point is None:
        lower = value.get("measured_complete_component_lower_bound", 0.)
        if value["realtime_status"] == "fail":
            if lower >= 1:
                return f"≥{lower:.5g} measured required work (fail, total unresolved)"
            return "fail (total unresolved)"
        return "unresolved"
    bounds = value.get(INTERVAL)
    point_text = f"{point:.5g}"
    if bounds is not None:
        point_text += f" [{bounds[0]:.5g}, {bounds[1]:.5g}]"
    return point_text + f" ({value['realtime_status']})"


def render_capacity_report(report):
    lines = ["# Native Robust manual-case capacity", "", f"Status **{report['status']}**.",
             "", "The estimates use configured public timing and keyed ordinals. The unit is elapsed processing seconds per baseband second.",
             "Nominal and conservative columns charge separately measured phase costs. The same-stream column measures the native scheduler directly.",
             "", "| Row | Nominal | Conservative | Measured stream | Unresolved work |",
             "|---|---:|---:|---:|---|"]
    for row in report["cases"]:
        heading = row["case"]["heading"].replace("|", "\\|").replace("\n", " ")
        gaps = "; ".join(row.get("unresolved_work", ())).replace("|", "\\|").replace("\n", " ")
        lines.append(f"| {row['case']['index']} {heading} | {_format(row.get('nominal'))} | "
                     f"{_format(row.get('conservative'))} | {_format(row.get('same_stream'))} | {gaps or 'None'} |")
    lines.extend(("", "The JSON retains exact input, allocation, work, uncertainty, margin, and workspace evidence.",
                  "Acquisition of unconfigured epochs, ordinals and timing is unqualified.", ""))
    return "\n".join(lines)


def _global_gate(probe, allocation_limits, *, enabled):
    if not enabled:
        return {"status": "explicitly_skipped", "passed": False}
    try:
        receipt = probe.call("message", **allocation_limits)
        result = receipt["result"]
        passed = (receipt["status"] == "complete" and result.get("complete_receiver_service") is True
                  and result.get("message_and_physical_quiet_verified") is True
                  and result.get("eof_control_verified") is True)
        return {"status": receipt["status"], "passed": passed, "receipt": receipt}
    except ProbeError as error:
        return {"status": "error", "passed": False, "reason": str(error)}


def run_cases(*, executable, output, case_list=DEFAULT_CASES, measurement_seconds=1., threads=None,
              case_indices=None, message_gate=True, sustained_allocation_probe=False,
              workspace_mib=None, full_logical_inference=None, calibration_stage=None,
              reuse_identical=False, slot_owners=None, deadline_ms=30_000,
              row_deadline_ms=180_000, max_samples=4_194_304, coarse_tiles=4096,
              target_cn0=None, fixed_active_seconds=None, backlog_slots=None,
              stream_looks=1, max_spool_bytes=None):
    """Use bounded native measurements for all selected rows in the current file."""
    parsed = load_cases(case_list)
    selected = select_cases(parsed, case_indices)
    if not .1 <= finite_number(measurement_seconds, "measurement unit seconds") <= 2:
        raise ProbeError("Measurement unit seconds must be in [0.1, 2]")
    if target_cn0 is not None:
        finite_number(target_cn0, "public profile C/N0 target")
    if fixed_active_seconds is not None and finite_number(fixed_active_seconds, "fixed active seconds") <= 0:
        raise ProbeError("Fixed active duration must be positive")
    if backlog_slots is not None and not 10 <= positive_int(backlog_slots, "stream backlog slots") <= 4096:
        raise ProbeError("Backlog probes require 10 to 4096 declared public slots")
    if type(stream_looks) is not int or stream_looks not in (1, 3, 5, 9):
        raise ProbeError("Stream looks must be one bounded observation or a declared 3/5/9 look")
    if calibration_stage not in (None, "noise-only-fine") or (calibration_stage and
            (not case_indices or workspace_mib is None or threads is not None or
             sustained_allocation_probe or full_logical_inference is False)):
        raise ProbeError("Noise-only calibration requires selected rows, fixed workspace and an automatic or full-logical allocation")
    if sustained_allocation_probe and threads is not None:
        raise ProbeError("Sustained allocation comparison requires the automatic worker pilot")
    limits = _limits(deadline_ms, row_deadline_ms, max_samples, coarse_tiles, workspace_mib, slot_owners)
    if max_spool_bytes is not None:
        limits["max_spool_bytes"] = positive_int(max_spool_bytes, "native capture spool bytes")
    probe = NativeProbe(executable)
    host = environment(probe.backend)
    description = probe.call("describe", workers=threads, **limits)
    policy = measurement_policy(len(parsed["cases"]), measurement_seconds)
    policy["native_call_limits"] = dict(limits)
    policy["row_deadline_ms"] = row_deadline_ms
    policy["maximum_native_calls_per_row"] = 42 + stream_looks
    policy["maximum_generated_samples_per_row"] = max_samples * policy["maximum_native_calls_per_row"]
    policy["backlog_slots"] = backlog_slots
    policy["stream_looks"] = stream_looks
    policy["requested_fixed_active_seconds"] = fixed_active_seconds
    policy["target_cn0_db_hz"] = 32. if target_cn0 is None else target_cn0
    profile_flags = {"target_cn0": target_cn0, "seconds": fixed_active_seconds}
    report = {"schema": SCHEMA, "status": "running", "description": description,
              "native_identity": description["identity"], "environment": host, "policy": policy,
              "selection": {"indices": [row["index"] for row in selected],
                            "current_list_entries": len(parsed["cases"]),
                            "complete_current_list": case_indices is None and calibration_stage is None,
                            "reuse_identical_normalized_inputs": reuse_identical,
                            "duration_policy": "explicit_fixed" if fixed_active_seconds is not None else "native_target_model",
                            "complete_worker_comparison": threads is None and calibration_stage is None},
              "input": {key: value for key, value in parsed.items() if key != "cases"},
              "scope": "configured_public_calendar_and_keyed_ordinal",
              "blind_fresh_start_capacity": unqualified_blind_capacity(), "cases": []}
    write_result(output, report)
    if description["status"] != "complete":
        reason = description.get("reason") or "Native allocation unavailable"
        report["status"] = "measured_with_unresolved_margins"
        report["message_gate"] = {"status": "unavailable", "passed": False, "reason": reason}
        report["cases"] = [{"case": row, "status": "unresolved", "unresolved_work": [reason]} for row in selected]
    else:
        resolved = allocation(description)
        limits["memory_limit_bytes"] = resolved["memory_limit_bytes"]
        topology = detect_cpu_topology(logical_cpu_ids=host["cpu_affinity"])
        workers = worker_plan(resolved["hardware_threads"], topology.get("physical_core_count"),
                              threads=threads, full_logical_inference=full_logical_inference)
        report["allocation"] = resolved
        report["physical_topology"] = topology
        report["worker_plan"] = workers
        if calibration_stage:
            report["calibration_stage"] = calibration_stage
        gate_workers = workers["inference_workers"] or resolved["workers"]
        report["message_gate"] = _global_gate(probe, limits | {"workers": gate_workers},
                                              enabled=message_gate and not calibration_stage)
        measured = {}
        for case in selected:
            print(f"Case {case['index']}/{len(parsed['cases'])} {case['heading']}", flush=True)
            config_hash = case["configuration_hash"]
            if reuse_identical and config_hash in measured:
                item = copy.deepcopy(measured[config_hash])
                item["reused_measurement_from"] = item["case"]["id"]
                item["case"] = case
            else:
                cutoff = time.monotonic() + row_deadline_ms / 1000
                calls = 0

                def invoke(command, **kwargs):
                    nonlocal calls
                    remaining = int(1000 * (cutoff - time.monotonic()))
                    if remaining < 1 or calls >= policy["maximum_native_calls_per_row"]:
                        raise ProbeError("Declared native row time or call limit reached")
                    calls += 1
                    bounded = limits | profile_flags | {"deadline_ms": min(deadline_ms, remaining)} | kwargs
                    receipt = probe.call(command, case["native_args"], **bounded)
                    if receipt["status"] == "complete" and (
                            allocation(receipt)["hardware_threads"] != resolved["hardware_threads"]
                            or runtime_identity(receipt) != runtime_identity(description)
                            or receipt["identity"].get("source_manifest_sha256") !=
                               description["identity"].get("source_manifest_sha256")):
                        raise ProbeError("Effective hardware, native build or runtime changed during the row")
                    return receipt

                try:
                    plan = invoke("plan", workers=workers["inference_workers"])
                    item = measure_complete_case(case, invoke=invoke, plan=plan,
                        worker_allocation=workers, family_size=policy["family_size"], policy=policy,
                        sustained_allocation_probe=sustained_allocation_probe, calibration_stage=calibration_stage)
                except (ValueError, OSError, KeyError) as error:
                    item = {"case": case, "status": "unresolved", "unresolved_work": [str(error)]}
                item["native_calls_launched"] = calls
                measured[config_hash] = item
            report["cases"].append(item)
            write_result(output, report)
        outcomes = [row["status"] for row in report["cases"]]
        if calibration_stage:
            report["status"] = ("stage_calibration_measured" if all(value == "stage_calibration_measured" for value in outcomes)
                                else "unresolved_stage_calibration")
        elif "capacity_failure" in outcomes:
            report["status"] = "capacity_failures"
        elif (all(value == "conditional_capacity_pass" for value in outcomes)
              and report["selection"]["complete_current_list"] and report["selection"]["complete_worker_comparison"]
              and report["message_gate"]["passed"] and workers["mode"] == "automatic_2H"):
            report["status"] = "conditional_capacity_pass"
        else:
            report["status"] = "measured_with_unresolved_margins"
    _verify_final(report, probe, parsed, host)
    write_result(output, report)
    if not calibration_stage:
        markdown = Path(output).with_suffix(".md")
        if markdown != Path(output):
            markdown.write_text(render_capacity_report(report), encoding="utf-8")
    print(f"{report['status']} {output}", flush=True)
    return report


def run_sensitivity(*, executable, output, case_list, case_indices, trials, cn0=None,
                    threads=None, slot_owners=None, workspace_mib=None, deadline_ms=30_000,
                    row_deadline_ms=180_000, max_samples=4_194_304, coarse_tiles=4096):
    """Independent native complete-text trials, with a separate observation budget."""
    parsed = load_cases(case_list)
    if not case_indices or type(trials) is not int or not 1 <= trials <= 29:
        raise ProbeError("Sensitivity requires explicit current case indices and 1 to 29 trials")
    selected = select_cases(parsed, case_indices)
    if trials == 29 and len(selected) != 1:
        raise ProbeError("A 29-trial conditional confirmation requires one declared case")
    if cn0 is not None:
        finite_number(cn0, "sensitivity C/N0")
    limits = _limits(deadline_ms, row_deadline_ms, max_samples, coarse_tiles, workspace_mib, slot_owners)
    probe = NativeProbe(executable)
    host = environment(probe.backend)
    description = probe.call("describe", workers=threads, **limits)
    if description["status"] == "complete":
        limits["memory_limit_bytes"] = allocation(description)["memory_limit_bytes"]
    report = {"schema": SENSITIVITY_SCHEMA, "status": "running", "input": {
              key: value for key, value in parsed.items() if key != "cases"},
              "description": description, "native_identity": description["identity"], "environment": host,
              "selection": [case["index"] for case in selected], "planned_trials_per_row": trials,
              "scope": "Independent native synthetic-channel e messages with two scored quiet slots",
              "policy": {"per_trial_limits": limits, "row_deadline_ms": row_deadline_ms,
                         "same_stream_capacity": False, "stop_on_first_failed_confirmation": trials == 29},
              "cases": []}
    for case in selected:
        row = {"case": case, "trials": [], "status": "unresolved"}
        report["cases"].append(row)
        cutoff = time.monotonic() + row_deadline_ms / 1000
        successes, complete = 0, 0
        try:
            plan = probe.call("plan", case["native_args"], workers=threads, **limits)
            row["plan"] = plan
            if plan["status"] != "complete":
                raise ProbeError(plan.get("reason") or "Native plan unavailable")
            target = cn0 if cn0 is not None else plan["configuration"]["target_cn0_db_hz"]
            row["target_cn0_db_hz"] = target
            for index in range(trials):
                remaining = int(1000 * (cutoff - time.monotonic()))
                if remaining < 1:
                    raise ProbeError("Declared sensitivity row time limit reached")
                options = limits | {"deadline_ms": min(deadline_ms, remaining)}
                receipt = probe.call("message", case["native_args"], workers=threads,
                                     cn0=target, seed=940000 + case["index"] * 100 + index, **options)
                row["trials"].append(receipt)
                result = receipt["result"]
                stream = result.get("stream") or {}
                slots = stream.get("slots") or []
                observation = receipt["observation"] or {}
                wire = observation.get("wire_bits_per_episode")
                if (stream.get("state") != "input_ended" or not wire or len(slots) != wire + 2
                        or any(slot.get("complete") is not True for slot in slots)):
                    raise ProbeError(receipt.get("reason") or "Independent native trial did not execute complete scored work")
                passed = (receipt["status"] == "complete" and result.get("message_and_physical_quiet_verified") is True
                          and result.get("eof_control_verified") is True)
                complete += 1
                successes += int(passed)
                if trials == 29 and not passed:
                    break
            row["status"] = "measured_independent_trials" if complete == trials else "incomplete_trial_budget"
        except (ValueError, KeyError) as error:
            row["reason"] = str(error)
        row.update(completed_trials=complete, successes=successes,
                   observed_exact_interval=success_interval(successes, complete) if complete else None,
                   one_sided_95_percent_lower_exceeds_90_percent=(
                       complete == trials and complete > 0 and success_interval(successes, complete)[0] > .90))
        write_result(output, report)
    report["status"] = ("measured_independent_trials" if all(
        row["status"] == "measured_independent_trials" for row in report["cases"]) else "unresolved")
    _verify_final(report, probe, parsed, host)
    write_result(output, report)
    return report
