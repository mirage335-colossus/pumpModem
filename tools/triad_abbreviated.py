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
import subprocess
import sys
import time

from triad_case_metrics import BATCHES, LOOKS, log_interval, t_critical
from triad_cases import DEFAULT_CASES
from triad_native import (BACKENDS, DEFAULT_PROBE, NativeProbe, ProbeError, allocation,
                          digest, environment, finite_number, is_sha256, named_phases, phase_once, positive_int,
                          read_report, runtime_identity, validate_receipt, write_result, json_loads, RECEIPT_LIMIT)

STRATA = ("active", "noise")
PHASES = ("native_slot", "coarse", "fine", "audio_frontend")
STREAM = "native_stream"
WORKLOADS = (*STRATA, STREAM)
KERNELS = tuple(f"{stratum}_{phase}" for stratum in STRATA for phase in PHASES) + (STREAM,)
ALPHA = .05
PRECISION = .15
MODEL_LIMIT = .30
SLOWDOWN = 1.30
SCHEMA = "datapump.triad.native-regression.v2"
STREAM_SAMPLE_LIMIT = 600_000
STREAM_EPOCHS = (1793850000, 1793850060, 1793850120, 1793850180, 1793850240)
STREAM_ORDINAL = 7000037
DEFAULT_BENCHMARK_DEADLINE_MS = 300_000
COMPONENT_SCHEMA = "datapump.triad.native-components.v1"
COMPONENT_IDENTITY_SCHEMA = "datapump.triad.native-component-identity.v1"
COMPONENT_RUNTIME = ("device", "device_name", "cuda", "cuda_compute_major", "cuda_compute_minor",
                     "cuda_runtime_version", "cuda_driver_version", "cuda_blas_version",
                     "library_version", "total_device_bytes")
COMPONENT_BUILD = ("source_manifest_sha256", "build_options_sha256", "compiler", "build_config")
MATRIX_SHAPES = ((769, 129, 65, 33, 2, 1024), (2048, 65, 64, 32, 2, 4096))


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
        "kernels": list(KERNELS),
        "claims_per_phase_look_pair": 3, "relative_precision": PRECISION,
        "maximum_holdout_error": MODEL_LIMIT, "material_slowdown_ratio": SLOWDOWN,
        "model": "Additive cost of identical complete native slot, audio frontend and five-slot stream units",
        "native_slot_unit": {
            "command": "regression", "native_units_per_call": 1,
            "execution": "First scored slot in a fresh process for every unit in each 1/2/4 batch",
            "scope": "Homogeneous first-slot execution; neither a fully cold GPU device nor steady-state service",
            "outer_budget": "All process startup, preparation and generation count toward the overall deadline",
        },
        "native_stream": {
            "command": "message", "slots": 5, "maximum_generated_samples": STREAM_SAMPLE_LIMIT,
            "expected_bits": [0, 0, 1], "expected_text": "e", "trailing_quiet_slots": 2,
            "pattern_epochs": list(STREAM_EPOCHS), "initial_symbol_ordinal": STREAM_ORDINAL,
            "unit": "One fresh native call; receiver_service includes receiver startup, ingress, drain, teardown and spool reads/hash",
            "excluded_service": "Process startup, profile preparation, generation, audio projection and the separate EOF control",
            "outer_budget": "All calls and excluded work count toward the overall benchmark deadline",
            "preflight": "One complete message call must pass before any statistical looks",
            "scope": "Complete fixed five-slot replay, not sustained backlog or absolute capacity",
        },
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


def regression_batch_list(receipts, stratum, count):
    """Sum homogeneous first-slot calls using the existing native phase validator."""
    if not isinstance(receipts, list) or len(receipts) != count:
        raise ProbeError("Native slot batch is missing a fresh-call unit")
    first = receipts[0]
    backend = first["identity"]["backend"]
    host = _comparison_environment(backend, first)
    observations = []
    for receipt in receipts:
        validate_receipt(receipt, command="regression", backend=backend, returncode=0)
        _same_native(receipt, first, host, backend)
        observations.append(regression_batch(receipt, stratum, 1))
    geometry = observations[0][1]
    if any(row[1] != geometry for row in observations):
        raise ProbeError("Native first-slot work or observation changed between fresh calls")
    return {phase: sum(row[0][phase] for row in observations) for phase in PHASES}, geometry


def stream_unit(receipt):
    """Validate the existing native message receipt without synthesizing DSP work."""
    backend = receipt.get("identity", {}).get("backend")
    validate_receipt(receipt, command="message", backend=backend, returncode=0)
    result, observation, config = receipt["result"], receipt["observation"], receipt["configuration"]
    stream = result.get("stream", {})
    resources = allocation(receipt)
    if (result.get("complete_receiver_service") is not True
            or result.get("message_and_physical_quiet_verified") is not True
            or result.get("eof_control_verified") is not True
            or result.get("expected_message_count") != 1
            or result.get("scope") != "configured_public_iq_stream"
            or stream.get("work_complete") is not True
            or stream.get("state") != "input_ended" or stream.get("input_closed") is not True
            or stream.get("committed_slots") != 5 or stream.get("next_public_slot") != 5):
        raise ProbeError("Native stream did not complete its five public slots and physical message")
    rows = positive_int(observation.get("samples"), "stream observation rows")
    if (rows > STREAM_SAMPLE_LIMIT or observation.get("first_sample") != 0
            or observation.get("stop_sample") != rows or observation.get("generated_samples") != rows
            or observation.get("channel_erased_rows") != 0
            or observation.get("expected_episodes") != 1 or observation.get("wire_bits_per_episode") != 3
            or stream.get("input_rows") != rows
            or stream.get("verified_input_sha256") != observation["input_sha256"]):
        raise ProbeError("Native stream observation or verified input extent is incomplete")
    slots, messages = stream.get("slots"), stream.get("messages")
    if not isinstance(slots, list) or len(slots) != 5 or not isinstance(messages, list) or len(messages) != 1:
        raise ProbeError("Native stream is missing slot or message receipts")
    ordinal = positive_int(config.get("initial_symbol_ordinal"), "initial stream ordinal", allow_zero=True)
    epoch = positive_int(config.get("calendar_epoch"), "configured stream epoch")
    if (epoch != STREAM_EPOCHS[0] or ordinal != STREAM_ORDINAL
            or observation.get("public_slot") != 0 or observation.get("symbol_ordinal") != ordinal
            or observation.get("pattern_epoch") != slots[0].get("pattern_epoch")):
        raise ProbeError("Native stream observation address differs from its configured origin")
    work_fields = ("coarse_workspaces", "coarse_frequency_points", "coarse_arrival_points", "fine_candidates",
                   "affine_blocks", "adaptive_blocks", "adaptive_peak_states", "adaptive_transition_edges",
                   "basis_rows", "quiet_modes")
    work = []
    previous_start = previous_stop = -1
    for index, slot in enumerate(slots):
        first = positive_int(slot.get("first_sample"), "slot first sample", allow_zero=True)
        stop = positive_int(slot.get("stop_sample"), "slot stop sample")
        address_epoch = positive_int(slot.get("pattern_epoch"), "slot pattern epoch")
        expected_bit = (0, 0, 1)[index] if index < 3 else None
        if (slot.get("index") != index or slot.get("symbol_ordinal") != ordinal + index
                or address_epoch != STREAM_EPOCHS[index]
                or first <= previous_start or stop <= previous_stop
                or stop <= first or stop > rows or slot.get("complete") is not True
                or slot.get("all_valid") is not True or slot.get("zero_h0_verified") is not True
                or slot.get("decision") != ("data" if index < 3 else "quiet")
                or (index < 3 and (type(slot.get("bit")) is not int or slot["bit"] != expected_bit))
                or (index >= 3 and slot.get("bit") is not None)):
            raise ProbeError("Native stream slot completion, bits or address chronology changed")
        previous_start, previous_stop = first, stop
        counts = {key: positive_int(slot.get(key), f"stream {key}", allow_zero=True) for key in work_fields}
        if any(counts[key] == 0 for key in ("coarse_workspaces", "coarse_frequency_points",
                                           "coarse_arrival_points", "fine_candidates", "affine_blocks", "basis_rows")):
            raise ProbeError("Native stream slot lacks complete useful receiver work")
        if index >= 3 and counts["quiet_modes"] == 0:
            raise ProbeError("Native stream completion lacks scored quiet evidence")
        worker_count = positive_int(slot.get("workers"), "stream slot workers")
        if worker_count > resources["workers"]:
            raise ProbeError("Native stream exceeded its worker allocation")
        commit = slot.get("commit", {})
        if positive_int(commit.get("input_stop"), "committed input stop") < stop:
            raise ProbeError("Native stream committed an incomplete public window")
        work.append({"index": index, "first_sample": first, "stop_sample": stop,
                     "pattern_epoch": address_epoch, "symbol_ordinal": ordinal + index,
                     "decision": slot["decision"], "bit": slot.get("bit"), **counts})
    if previous_stop != rows:
        raise ProbeError("Native stream did not score its declared final window")
    message = messages[0]
    expected_message = {"first_public_slot": 0, "stop_public_slot": 5, "physical_complete": True,
                        "expected_source_matches": True, "long_form": False, "observed_bits": 3,
                        "missing_slots": 0, "trailing_quiet_slots": 2}
    if any(type(message.get(key)) is not type(value) or message[key] != value
           for key, value in expected_message.items()):
        raise ProbeError("Native stream did not decode e after exactly two scored quiet slots")
    reserve = positive_int(stream.get("spool_residency_reserved_bytes"), "spool reservation", allow_zero=True)
    receiver_peak = positive_int(stream.get("receiver_peak_reserved_bytes"), "receiver peak memory")
    receiver_limit = positive_int(stream.get("receiver_memory_limit_bytes"), "receiver memory limit")
    peak = positive_int(stream.get("peak_reserved_bytes"), "application peak memory")
    if (reserve != observation.get("spool_residency_reserved_bytes")
            or receiver_limit + reserve != resources["memory_limit_bytes"]
            or stream.get("total_application_memory_limit_bytes") != resources["memory_limit_bytes"]
            or receiver_peak > receiver_limit or peak != receiver_peak + reserve
            or peak > resources["memory_limit_bytes"]):
        raise ProbeError("Native stream memory receipt exceeds or changes its allocation")
    for field, limit in (("peak_active_owners", resources["slot_owners"]),
                         ("peak_running_owners", resources["slot_owners"]),
                         ("peak_executor_workers", resources["workers"])):
        # Earlier native references lack the newer pool telemetry. Never invent it.
        if field in stream and positive_int(stream[field], field, allow_zero=True) > limit:
            raise ProbeError("Native stream pool telemetry exceeds its allocation")
    phase = phase_once(receipt, "receiver_service")
    seconds = finite_number(phase.get("wall_seconds"), "receiver service", nonnegative=True)
    measured = phase["work"]
    if (seconds <= 0 or seconds != stream.get("wall_seconds")
            or phase.get("unit_index") != 0
            or phase.get("scope") != "native_iq_replay_including_bounded_spool_reads_and_verification"
            or measured.get("committed_slots") != 5 or measured.get("input_rows") != rows
            or measured.get("verified_input_sha256") != observation["input_sha256"]
            or measured.get("allocation") != receipt["allocation"]):
        raise ProbeError("Native receiver_service does not describe the validated stream")
    eof = phase_once(receipt, "eof_control")["work"]
    if (eof.get("work_complete") is not True or eof.get("committed_slots") != 1
            or eof.get("messages") != [] or len(eof.get("slots", [])) != 1
            or eof["slots"][0].get("decision") != "data"):
        raise ProbeError("Native stream EOF control did not leave the partial message incomplete")
    geometry = {"configuration": config, "allocation": resources, "observation": observation,
                "work_identity_sha256": digest({"slots": work, "message": expected_message})}
    return seconds, geometry


def stream_batch(receipts, count):
    if not isinstance(receipts, list) or len(receipts) != count:
        raise ProbeError("Native stream batch is missing a fresh-call unit")
    observations = [stream_unit(receipt) for receipt in receipts]
    geometry = observations[0][1]
    if any(row[1] != geometry for row in observations):
        raise ProbeError("Native stream work or observation changed between units")
    return {STREAM: sum(row[0] for row in observations)}, geometry


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


def _same_native(receipt, description, host, backend):
    if (any(receipt["identity"].get(key) != description["identity"].get(key)
            for key in ("source_manifest_sha256", "executable_sha256"))
            or _comparison_environment(backend, receipt) != host):
        raise ProbeError("Native regression runtime, executable or allocation changed between calls")


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
    if not isinstance(batches, dict) or set(batches) != set(WORKLOADS):
        raise ProbeError("Baseline lacks complete native batch evidence")
    preflight = baseline.get("stream_preflight")
    _, preflight_geometry = stream_unit(preflight)
    _same_native(preflight, desc, host, backend)
    if preflight_geometry != baseline["geometry"][STREAM]:
        raise ProbeError("Baseline stream preflight differs from its measured work")
    for stratum, batch in batches.items():
        rows = {}
        for sample in batch:
            index = positive_int(sample["look_row"], "baseline look row")
            units = positive_int(sample["units"], "baseline batch units")
            sizes = rows.setdefault(index, {})
            if units not in BATCHES or units in sizes:
                raise ProbeError("Baseline repeated or undeclared native unit batch")
            if stratum == STREAM:
                timings, geometry = stream_batch(sample.get("receipts"), units)
            else:
                timings, geometry = regression_batch_list(sample.get("receipts"), stratum, units)
            for receipt in sample["receipts"]:
                _same_native(receipt, desc, host, backend)
            if geometry != baseline["geometry"][stratum]:
                raise ProbeError("Baseline native observation or work differs from its declared geometry")
            sizes[units] = timings
        if len(rows) not in LOOKS or sorted(rows) != list(range(1, len(rows) + 1)):
            raise ProbeError("Baseline lacks complete declared outer looks")
        for phase in ((STREAM,) if stratum == STREAM else PHASES):
            name = STREAM if stratum == STREAM else f"{stratum}_{phase}"
            observed = [[rows[index][units][phase] for units in BATCHES] for index in range(1, len(rows) + 1)]
            if observed != _baseline_kernel(baseline, name, target_units)["batch_seconds"]:
                raise ProbeError("Baseline timing summaries do not match native phase observations")


def _run_native(probe, report, *, target_units=1000, device="cpu", baseline=None,
        workers=None, slot_owners=None, memory_limit_bytes=None, deadline_ms=30_000,
        max_samples=4_194_304, tensor_precision=None, tensor_coarse_math=None,
        device_index=None, device_memory_limit_bytes=None):
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
               "tensor_precision": tensor_precision, "tensor_coarse_math": tensor_coarse_math, "device_index": device_index,
               "device_memory_limit_bytes": device_memory_limit_bytes}
    description = probe.call("describe", **options)
    report.update(native_identity=description["identity"], description=description,
                  geometry={}, kernels={}, batches={},
                  baseline_identity=baseline.get("native_identity") if baseline else None)
    if description["status"] != "complete":
        return report | {"status": description["status"], "reason": description.get("reason")}
    host = _comparison_environment(device, description)
    report["environment"] = host
    options["memory_limit_bytes"] = host["allocation"]["memory_limit_bytes"]
    if baseline is not None:
        validate_baseline(baseline, baseline.get("geometry"), host)
        if set(baseline["geometry"]) != set(WORKLOADS):
            raise ProbeError("Baseline is missing a native workload stratum")
        _validate_baseline_evidence(baseline, host, device, target_units)
    stream_options = options | {"slots": 5, "max_samples": min(max_samples, STREAM_SAMPLE_LIMIT)}
    preflight = probe.call("message", **stream_options)
    report["stream_preflight"] = preflight
    if preflight["status"] != "complete":
        return report | {"status": preflight["status"], "reason": preflight.get("reason")}
    _same_native(preflight, description, host, device)
    _, report["geometry"][STREAM] = stream_unit(preflight)
    if baseline is not None and baseline["geometry"][STREAM] != report["geometry"][STREAM]:
        raise ProbeError("Baseline native stream preflight is inapplicable")
    for stratum in WORKLOADS:
        phases = (STREAM,) if stratum == STREAM else PHASES
        rows = {phase: [] for phase in phases}
        receipts = report["batches"].setdefault(stratum, [])
        for look in LOOKS:
            while len(rows[phases[0]]) < look:
                row = {phase: [None] * len(BATCHES) for phase in phases}
                offset = len(rows[phases[0]])
                for index in ((offset + turn) % 3 for turn in range(3)):
                    units = BATCHES[index]
                    fresh = []
                    sample = {"look_row": offset + 1, "units": units, "receipts": fresh}
                    receipts.append(sample)
                    command = "message" if stratum == STREAM else "regression"
                    call_options = stream_options if stratum == STREAM else options | {"stratum": stratum, "units": 1}
                    for _ in range(units):
                        receipt = probe.call(command, **call_options)
                        fresh.append(receipt)
                        if receipt["status"] != "complete":
                            return report | {"status": receipt["status"], "reason": receipt.get("reason")}
                        _same_native(receipt, description, host, device)
                    if stratum == STREAM:
                        times, geometry = stream_batch(fresh, units)
                    else:
                        times, geometry = regression_batch_list(fresh, stratum, units)
                    if report["geometry"].setdefault(stratum, geometry) != geometry:
                        raise ProbeError("Native regression observation, configuration or work changed between batches")
                    if baseline is not None and baseline["geometry"][stratum] != geometry:
                        raise ProbeError("Baseline native observation, geometry or work is inapplicable")
                    for phase, seconds in times.items():
                        row[phase][index] = seconds
                for phase, seconds in row.items():
                    rows[phase].append(seconds)
            for phase, measured in rows.items():
                name = STREAM if stratum == STREAM else f"{stratum}_{phase}"
                reference = _baseline_kernel(baseline, name, target_units) if baseline else None
                report["kernels"][name] = analyze(measured, target_units, baseline=reference)
            if all(report["kernels"][STREAM if stratum == STREAM else f"{stratum}_{phase}"]["stop"] for phase in phases):
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


class BenchmarkExpired(ProbeError):
    def __init__(self, reason, receipt=None):
        super().__init__(reason)
        self.receipt = receipt


class BoundedProbe:
    """Charge process overhead and all excluded work to one wall-clock budget."""
    def __init__(self, executable, backend, deadline_ms):
        self.deadline_ms = positive_int(deadline_ms, "benchmark deadline")
        self.started = time.monotonic()
        self.calls = 0
        self.probe = NativeProbe(executable, backend=backend)

    def elapsed_ms(self):
        return (time.monotonic() - self.started) * 1000

    def call(self, command, **options):
        # NativeProbe permits 15 seconds for cooperative cancellation/exit.
        remaining = int(self.deadline_ms - self.elapsed_ms()) - 15_000
        if remaining <= 0:
            raise BenchmarkExpired("Overall native regression deadline exhausted before another complete call")
        options["deadline_ms"] = min(options.get("deadline_ms", 30_000), remaining)
        self.calls += 1
        receipt = self.probe.call(command, **options)
        if self.elapsed_ms() >= self.deadline_ms:
            raise BenchmarkExpired("Overall native regression deadline exhausted", receipt)
        return receipt

    def verify_unchanged(self):
        self.probe.verify_unchanged()
        if self.elapsed_ms() >= self.deadline_ms:
            raise BenchmarkExpired("Overall native regression deadline exhausted")


def run(*, executable=DEFAULT_PROBE, device="cpu", benchmark_deadline_ms=DEFAULT_BENCHMARK_DEADLINE_MS, **options):
    probe = BoundedProbe(executable, device, benchmark_deadline_ms)
    report = {"schema": SCHEMA, "policy": policy()}
    try:
        report = _run_native(probe, report, device=device, **options)
    except BenchmarkExpired as error:
        report.update(status="incomplete", reason=str(error))
        if error.receipt is not None:
            report["deadline_last_call"] = error.receipt
    elapsed = probe.elapsed_ms()
    if elapsed >= probe.deadline_ms:
        report.update(status="incomplete", reason="Overall native regression deadline exhausted")
    report["run_budget"] = {"deadline_ms": probe.deadline_ms, "elapsed_ms": elapsed,
                            "native_calls": probe.calls, "includes_all_generation_and_controls": True}
    return report


def component_identity(record, description, previous=None):
    """Bind the native fixture to the production probe, without calling it DSP work."""
    if not isinstance(record, dict) or record.get("schema") != COMPONENT_IDENTITY_SCHEMA:
        raise ProbeError("Missing native component identity")
    native = description["identity"]
    bulk = native.get("backend_receipt")
    if not isinstance(bulk, dict) or bulk.get("cuda") is not True or record.get("cuda") is not True:
        raise ProbeError("Components require an actual CUDA backend")
    if record.get("actual_device_work") is not False:
        raise ProbeError("Component identity must not claim DSP execution")
    for name in (*COMPONENT_BUILD, *COMPONENT_RUNTIME):
        expected = native.get(name) if name in COMPONENT_BUILD else bulk.get(name)
        if expected is None or record.get(name) != expected:
            raise ProbeError(f"Component/probe identity mismatch: {name}")
    for name in ("source_manifest_sha256", "build_options_sha256", "fixture_source_sha256"):
        if not is_sha256(record.get(name)):
            raise ProbeError(f"Invalid component {name}")
    for name in ("total_device_bytes", "initial_free_device_bytes", "workspace_limit_bytes"):
        positive_int(record.get(name), name)
    if previous is not None and any(record.get(name) != previous.get(name)
                                    for name in (*COMPONENT_BUILD, *COMPONENT_RUNTIME, "fixture_source_sha256")):
        raise ProbeError("Component source/build/runtime changed between calls")
    return record


def _component_integer(row, name, *, zero=True):
    return positive_int(row.get(name), name, allow_zero=zero)


def _component_time(row, name, *, positive=False):
    value = finite_number(row.get(name), name, nonnegative=True)
    if positive and value <= 0:
        raise ProbeError(f"Component {name} must be positive")
    return value


def validate_component_rows(rows, component, matrix_case, workspace_bytes, identity):
    """Check a fixed three-pair native component look; never extrapolate to reception."""
    if len(rows) != 6 or any(not isinstance(row, dict) for row in rows):
        raise ProbeError("Component benchmark requires exactly three complete paired looks")
    matrix = component != "peaks"
    shape = MATRIX_SHAPES[matrix_case] if matrix else (8989, 32, 10553, 78, 2, 32768)
    geometry_names = ("kernel", "frequencies", "arrivals", "workspaces", "taps", "correlation_fft_rows")
    expected_schema = "native_coarse_bulk_benchmark_v1" if matrix else "native_arrival_peak_benchmark_v1"
    expected_scope = "post_channelization_CoarseBulk" if matrix else "synthetic_native_bulk_geometry"
    modes = ("fft", component) if matrix else ("full_scores_host_nms", "cuda_frontier")
    scores = shape[1] * shape[2] * shape[3]
    pairs = shape[1] * shape[3]
    fingerprint = None
    for index, row in enumerate(rows):
        look, mode = index // 2 + 1, modes[index % 2]
        order_name = "candidate_first" if matrix else "compressed_first"
        if (row.get("schema") != expected_schema or row.get("scope") != expected_scope
                or type(row.get("look")) is not int or row["look"] != look
                or row.get("math" if matrix else "mode") != mode
                or row.get(order_name) is not (look % 2 == 0)):
            raise ProbeError("Component schema, look, order or arithmetic changed")
        if tuple(_component_integer(row, name, zero=False) for name in geometry_names) != shape:
            raise ProbeError("Component public geometry changed")
        value = row.get("input_fnv1a64")
        if not isinstance(value, str) or not value.isascii() or not value.isdecimal() or not 0 <= int(value) < 2**64:
            raise ProbeError("Component lacks its native input fingerprint")
        if fingerprint is None:
            fingerprint = value
        if value != fingerprint:
            raise ProbeError("Component input changed between paired looks")
        if row.get("initialized_device") is not True or row.get("fresh_owner_excludes_teardown") is not True:
            raise ProbeError("Component timing boundary changed")
        parts = ("owner_seconds", "prepare_seconds", "first_lag_seconds" if matrix else "search_seconds")
        part_sum = sum(_component_time(row, name) for name in parts)
        fresh = _component_time(row, "fresh_owner_seconds", positive=True)
        if not math.isclose(fresh, part_sum, rel_tol=1e-10, abs_tol=1e-12):
            raise ProbeError("Component fresh-owner time does not include preparation and search")
        if _component_integer(row, "workspace_limit_bytes", zero=False) != workspace_bytes:
            raise ProbeError("Component changed the exact workspace bound")
        for name in ("peak_reserved_bytes", "allocator_peak_reserved_bytes"):
            if _component_integer(row, name, zero=False) > workspace_bytes:
                raise ProbeError("Component exceeded its workspace bound")
        for name in ("upload_bytes", "download_bytes", "synchronizations"):
            _component_integer(row, name, zero=False)
        if _component_integer(row, "score_rows", zero=False) != scores * (2 if matrix else 1):
            raise ProbeError("Component did not process the complete score grid")
        if matrix:
            if (type(row.get("benchmark_case")) is not int or row["benchmark_case"] != matrix_case
                    or row.get("comparison_math") != component
                    or row.get("input_hash_encoding") != "fnv1a64_le_geometry_observation_templates_and_lag_plan_v1"
                    or row.get("resident_reuses_immutable_input") is not True
                    or _component_integer(row, "independent_dft_checks", zero=False) != 19):
                raise ProbeError("Matrix comparison or numerical oracle contract changed")
            _component_time(row, "resident_lag_seconds", positive=True)
            error = _component_time(row, "normalized_score_error")
            if error > (3e-5 if mode == "fft" else .002 if mode == "fp16" else .01):
                raise ProbeError("Matrix score error exceeded its frozen numerical gate")
            for name in ("cuda_runtime_version", "cuda_driver_version", "cuda_blas_version",
                         "cuda_compute_major", "cuda_compute_minor"):
                if row.get(name) != identity[name]:
                    raise ProbeError(f"Matrix runtime changed: {name}")
            counts = [_component_integer(row, f"matrix_{name}_calls") for name in ("fp16", "int8")]
            calls = _component_integer(row, "matrix_calls")
            fallback = row.get("matrix_fft_fallback_counts")
            if (not isinstance(fallback, list) or len(fallback) != 6
                    or any(type(count) is not int or count != 0 for count in fallback)
                    or _component_integer(row, "matrix_nonfinite_values")
                    or _component_integer(row, "matrix_saturated_values")):
                raise ProbeError("Matrix benchmark fell back or produced invalid packed values")
            if mode == "fft":
                if calls or any(counts) or _component_integer(row, "fft_calls", zero=False) <= 0:
                    raise ProbeError("FFT reference executed another arithmetic path")
            else:
                if not calls or sum(counts) != calls or counts[0 if mode == "fp16" else 1] != calls:
                    raise ProbeError("Requested matrix arithmetic did not execute exclusively")
                if (_component_integer(row, "matrix_inner_chunks", zero=False) != calls
                        or _component_integer(row, "matrix_output_values", zero=False) != 2 * shape[4] * scores
                        or tuple(_component_integer(row, name, zero=False) for name in (
                            "matrix_maximum_rows", "matrix_maximum_columns", "matrix_maximum_inner"))
                        != (4096, shape[1], shape[0])):
                    raise ProbeError("Matrix benchmark changed the admitted work or batch")
                for name in ("matrix_padded_real_macs", "matrix_packed_values"):
                    _component_integer(row, name, zero=False)
        else:
            if row.get("separation") != 17 or row.get("independent_dft_points") != 16:
                raise ProbeError("Peak numerical oracle or selection geometry changed")
            forward, inverse = shape[1], pairs * shape[4]
            expected_stages = (forward + inverse) * shape[5] * (shape[5].bit_length() - 1)
            if (_component_integer(row, "forward_ffts", zero=False) != forward
                    or _component_integer(row, "inverse_ffts", zero=False) != inverse
                    or _component_integer(row, "fft_row_stages", zero=False) != expected_stages):
                raise ProbeError("Peak optimization changed full FFT work")
            compressed = mode == "cuda_frontier"
            if (_component_integer(row, "cuda_arrival_peak_rows") != (pairs if compressed else 0)
                    or _component_integer(row, "cuda_arrival_peak_values") != (scores if compressed else 0)
                    or row["download_bytes"] != (pairs * 40 if compressed else scores * 8)):
                raise ProbeError("Peak frontier did not preserve full search/readback accounting")
    phases = ("fresh_owner_seconds", "resident_lag_seconds") if matrix else ("fresh_owner_seconds", "search_seconds")
    comparisons = {}
    for phase in phases:
        ratios = [rows[index + 1][phase] / rows[index][phase] for index in (0, 2, 4)]
        point, interval = log_interval(ratios, alpha=.05)
        comparisons[phase] = {"paired_candidate_over_reference": ratios,
                              "geometric_mean_ratio": point, "descriptive_95_percent_interval": interval,
                              "scope": "Three fixed paired component observations, not a capacity or regression verdict"}
    return {"component": component, "status": "component_checks_passed", "input_fnv1a64": fingerprint,
            "actual_algorithm": "scaled_lag_matrix" if matrix else "full_fft_with_exact_cuda_frontier",
            "tensor_core_instruction_evidence": "not_collected_by_this_timing_test",
            "rows": rows, "comparisons": comparisons}


def run_components(*, executable=DEFAULT_PROBE, component_executable=None, components="all", matrix_case=0,
                   workspace_mib=2048, benchmark_deadline_ms=DEFAULT_BENCHMARK_DEADLINE_MS):
    """Run only the declared native component comparisons, under one wall budget."""
    started = time.monotonic()
    deadline = positive_int(benchmark_deadline_ms, "benchmark deadline")
    positive_int(workspace_mib, "device workspace MiB")
    if workspace_mib > 65536 or components not in ("fp16", "int8", "peaks", "all"):
        raise ProbeError("Invalid component or workspace selection")
    if type(matrix_case) is not int or matrix_case not in (0, 1):
        raise ProbeError("Component matrix case must be 0 or 1")
    selected = ("fp16", "int8", "peaks") if components == "all" else (components,)
    if "peaks" in selected and workspace_mib != 2048:
        raise ProbeError("The fixed peak comparison requires exactly 2048 MiB")
    report = {"schema": COMPONENT_SCHEMA, "status": "incomplete", "environment": environment("cuda"),
              "policy": {"components": list(selected), "matrix_case": matrix_case,
                         "device_workspace_bytes": workspace_mib * 2**20, "paired_looks": 3,
                         "maximum_benchmark_calls": len(selected), "maximum_native_calls": len(selected) + 2,
                         "matrix_scorings_per_comparison": 14, "peak_scorings": 8,
                         "receipt_acceptance_limit_bytes": RECEIPT_LIMIT,
                         "selection": "Fixed before measurements; no retries or timing-based expansion",
                         "device_selection": "Visible CUDA device 0; use CUDA_VISIBLE_DEVICES before launch"},
              "complete_receiver_capacity_qualified": False, "cpu_forecast_qualified": False,
              "relative_commit_regression_pass": False, "calls": [], "components": []}
    receiver = component = None

    def elapsed_ms():
        return (time.monotonic() - started) * 1000

    def verify():
        receiver.verify_unchanged()
        component.verify_unchanged()
        if environment("cuda") != report["environment"]:
            raise ProbeError("Host environment changed during component measurements")
        if elapsed_ms() >= deadline:
            raise BenchmarkExpired("Overall component deadline exhausted")

    def invoke(options, maximum_rows):
        verify()
        record = {"argv": [str(component.path), "--cuda", *options]}
        report["calls"].append(record)
        begin = time.monotonic()
        try:
            finished = subprocess.run(record["argv"], capture_output=True, check=False,
                                      timeout=max(.001, (deadline - elapsed_ms()) / 1000))
            stdout, stderr = finished.stdout, finished.stderr
            record["returncode"] = finished.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b"", error.stderr or b""
            record["timed_out"] = True
        record.update(elapsed_seconds=time.monotonic() - begin,
                      stdout=stdout[:RECEIPT_LIMIT].decode("utf-8", errors="replace"),
                      stderr=stderr[-4096:].decode("utf-8", errors="replace"))
        verify()
        if record.get("timed_out"):
            raise BenchmarkExpired("Native component exceeded its bounded process deadline")
        if len(stdout) > RECEIPT_LIMIT or len(stderr) > RECEIPT_LIMIT:
            raise ProbeError("Native component output exceeds its size limit")
        if record["returncode"] != 0:
            raise ProbeError(f"Native component check failed (exit {record['returncode']}): {record['stderr'].strip()}")
        lines = stdout.splitlines()
        if len(lines) != maximum_rows:
            raise ProbeError("Native component returned incomplete or additional JSONL records")
        return [json_loads(line) for line in lines]

    try:
        receiver = NativeProbe(executable, backend="cuda")
        component = NativeProbe(component_executable or receiver.path.with_name(
            "test_triad_tensor" + receiver.path.suffix), backend="cuda")
        report["binaries"] = {"receiver": {"path": str(receiver.path), "sha256": receiver.executable_sha256},
                              "components": {"path": str(component.path), "sha256": component.executable_sha256}}
        verify()
        remaining = int(deadline - elapsed_ms()) - 15000
        if remaining <= 0:
            raise BenchmarkExpired("Overall component deadline cannot admit receiver description")
        description_call = {"command": "receiver_describe"}
        report["calls"].append(description_call)
        begin = time.monotonic()
        description = receiver.call("describe", deadline_ms=min(30000, remaining),
                                    device_memory_limit_bytes=workspace_mib * 2**20)
        description_call.update(elapsed_seconds=time.monotonic() - begin, receipt=description)
        verify()
        if description["status"] != "complete":
            report.update(status=description["status"], reason=description.get("reason"))
            return report
        identity = component_identity(invoke(["--component-describe"], 1)[0], description)
        report["native_identity"] = identity
        for choice in selected:
            options = (["--peak-benchmark"] if choice == "peaks" else ["--matrix-benchmark", "--matrix-math", choice,
                       "--matrix-benchmark-case", str(matrix_case), "--matrix-benchmark-workspace-mib", str(workspace_mib)])
            records = invoke(options, 7)
            current_identity = component_identity(records[0], description, identity)
            if current_identity["workspace_limit_bytes"] != workspace_mib * 2**20:
                raise ProbeError("Component identity changed the exact benchmark workspace")
            checked = validate_component_rows(records[1:], choice, matrix_case,
                                              workspace_mib * 2**20, current_identity)
            if choice != "peaks" and any(prior["component"] != "peaks" and
                    prior["input_fnv1a64"] != checked["input_fnv1a64"] for prior in report["components"]):
                raise ProbeError("Matrix arithmetic comparisons changed the fixed input")
            report["components"].append(checked)
        verify()
        report.update(status="component_checks_passed", identity_verified_at_completion=True)
    except BenchmarkExpired as error:
        report.update(status="incomplete", reason=str(error))
    except (ValueError, OSError, KeyError, TypeError, UnicodeError) as error:
        report.update(status="error", reason=str(error))
    finally:
        report["run_budget"] = {"deadline_ms": deadline, "elapsed_ms": elapsed_ms(),
                                "native_calls": len(report["calls"]), "includes_process_and_control_overhead": True}
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--device", choices=BACKENDS, default="cpu")
    parser.add_argument("--tensor-precision", choices=("automatic", "fp64"))
    parser.add_argument("--tensor-coarse-math", choices=("fft", "fp16", "int8"))
    parser.add_argument("--device-index", type=int)
    parser.add_argument("--device-workspace-mib", type=int)
    parser.add_argument("--components", choices=("fp16", "int8", "peaks", "all"),
                        help="Bounded native component comparisons, separate from receiver regression/capacity.")
    parser.add_argument("--component-probe", type=Path, help="Compiled test_triad_tensor; defaults beside --probe.")
    parser.add_argument("--matrix-case", type=int, choices=(0, 1),
                        help="One predeclared native matrix geometry (default 0).")
    parser.add_argument("--target-units", type=int, default=1000, help="Projection only. Executes bounded 1/2/4 batches.")
    parser.add_argument("--cases", nargs="?", const=str(DEFAULT_CASES), help="Use the manual capacity entry point.")
    parser.add_argument("--probe", "--exporter", type=Path, dest="probe", default=DEFAULT_PROBE,
                        help="Compiled receiver probe. The legacy exporter option selects the sibling probe.")
    parser.add_argument("--probe-seconds", type=float, default=1.)
    parser.add_argument("--threads", type=int, help="Exact worker override. Omitted uses 2H.")
    parser.add_argument("--slot-owners", type=int, help="Exact native slot owner limit.")
    parser.add_argument("--workspace-mib", type=int, help="Exact native host memory limit.")
    parser.add_argument("--deadline-ms", type=int, default=30_000, help="Deadline per bounded native call.")
    parser.add_argument("--benchmark-deadline-ms", type=int, default=DEFAULT_BENCHMARK_DEADLINE_MS,
                        help="Overall regression deadline, including generation, controls and process overhead.")
    parser.add_argument("--max-samples", type=int, default=4_194_304, help="Generated sample cap per native call.")
    parser.add_argument("--case-index", type=int, action="append")
    parser.add_argument("--skip-message-gate", action="store_true")
    input_args = list(sys.argv[1:] if argv is None else argv)
    args = parser.parse_args(input_args)
    explicit = {arg.split("=", 1)[0] for arg in input_args if arg.startswith("--")}
    if args.components is not None:
        allowed = {"--output", "--device", "--probe", "--components", "--component-probe", "--matrix-case",
                   "--device-workspace-mib", "--benchmark-deadline-ms"}
        if explicit - allowed:
            parser.error("Receiver regression/manual options do not apply to component comparisons")
        if args.device != "cuda":
            parser.error("Component timing requires --device cuda; CPU execution does not qualify a GPU")
        if args.components == "peaks" and args.matrix_case is not None:
            parser.error("--matrix-case does not apply to the fixed peak comparison")
        try:
            result = run_components(executable=args.probe, component_executable=args.component_probe,
                                    components=args.components, matrix_case=args.matrix_case or 0,
                                    workspace_mib=args.device_workspace_mib if args.device_workspace_mib is not None else 2048,
                                    benchmark_deadline_ms=args.benchmark_deadline_ms)
        except (ValueError, OSError, KeyError, TypeError) as error:
            result = {"schema": COMPONENT_SCHEMA, "status": "error", "reason": str(error)}
        write_result(args.output, result)
        print(f"{result['status']}: {args.output}")
        if result.get("reason"):
            print(result["reason"], file=sys.stderr)
        return 0 if result["status"] == "component_checks_passed" else 2 if result["status"] in ("incomplete", "unavailable") else 1
    if args.component_probe is not None or args.matrix_case is not None:
        parser.error("--component-probe and --matrix-case require --components")
    if args.cases is not None:
        if explicit & {"--baseline", "--device", "--target-units", "--benchmark-deadline-ms", "--tensor-precision", "--tensor-coarse-math",
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
            args.tensor_precision, args.tensor_coarse_math, args.device_index, args.device_workspace_mib)):
        parser.error("Tensor options require --device tensor-cpu or cuda")
    try:
        options = {"target_units": args.target_units, "device": args.device,
                   "baseline": read_report(args.baseline) if args.baseline else None,
                   "benchmark_deadline_ms": positive_int(args.benchmark_deadline_ms, "benchmark deadline")}
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
        if args.tensor_coarse_math is not None:
            options["tensor_coarse_math"] = args.tensor_coarse_math
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
