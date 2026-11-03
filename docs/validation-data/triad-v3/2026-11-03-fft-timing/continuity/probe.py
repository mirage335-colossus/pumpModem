#!/usr/bin/env python3
"""Paired, full-bank intermediate channel-window screen on restored captures."""
from __future__ import annotations

import concurrent.futures
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / "local-evidence" / "triad-continuity"
RESTORED = LOCAL / "restored"
SCORES = LOCAL / "scores"
PYTHON = Path("/tmp/triad-weak-env/bin/python")
RECEIVER = ROOT / "tools" / "triad_group_acquire.py"
LIMIT_SECONDS = 600.0
CHILD_LIMIT_SECONDS = 90.0
MAX_PARALLEL = 2
INPUTS = ("manifest.json", "group_gram.c128", "group_observation.c128",
          "received_energy.f64", "intervals.u64", "valid_rows.u64")

CASES = (
    dict(name="loss-random25-validation-11-b1-signal",
         workspace="validation-schedule-6/workspaces/loss-random25-validation-11-b1-signal",
         reports="additional-physical-reports/validation-schedule-6-rescored/scores"),
    dict(name="none-am65-validation-07-b1-signal",
         workspace="validation-light-12/workspaces/none-am65-validation-07-b1-signal",
         reports="physical-reports/validation-light-12-rescored/scores"),
    dict(name="none-am65-validation-40-b0-null",
         workspace="validation-light-12/workspaces/none-am65-validation-40-b0-null",
         reports="physical-reports/validation-light-12-rescored/scores"),
    dict(name="none-am65-validation-41-b1-wrong-key",
         workspace="validation-light-12/workspaces/none-am65-validation-41-b1-wrong-key",
         reports="physical-reports/validation-light-12-rescored/scores"),
    dict(name="shared-microwave-am65-validation-41-b1-wrong-key",
         workspace="validation-light-12/workspaces/shared-microwave-am65-validation-41-b1-wrong-key",
         reports="physical-reports/validation-light-12-rescored/scores"),
)

MODELS = tuple(
    dict(name=f"width-{width:g}-window-{window}", doppler_width_hz=width,
         window_blocks=window, window_seconds=window * 0.125)
    for width in (0., 0.2) for window in (4, 16, 32)
)
BASELINES = ("global", "window", "global-frequency")
_print_lock = threading.Lock()


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.hexdigest()


def read_json(path: Path):
    return json.loads(path.read_text())


def ensure_fresh():
    if (LOCAL / "summary.json").exists():
        raise FileExistsError(f"refusing to overwrite prior probe summary: {LOCAL / 'summary.json'}")
    if not RECEIVER.is_file() or not PYTHON.is_file():
        raise FileNotFoundError("bound receiver or Python environment is missing")


def preflight():
    source_hash = sha(RECEIVER)
    workspaces = []
    for item in CASES:
        workspace = RESTORED / item["workspace"]
        manifest = read_json(workspace / "manifest.json")
        if manifest.get("format") != "triad-group-block-stats-v1":
            raise ValueError(f"unexpected workspace format: {workspace}")
        inputs = {name: sha(workspace / name) for name in INPUTS}
        for method in BASELINES:
            report_path = (ROOT / "local-evidence" / "triad-extension" / "remote" /
                           item["reports"] / f"rescore-{method}-{item['name']}.json")
            report = read_json(report_path)
            if report.get("receiver_source_sha256") != source_hash:
                raise ValueError(f"historical receiver hash mismatch: {report_path}")
            if len(report.get("captures", [])) != 1:
                raise ValueError(f"historical report is not one capture: {report_path}")
            if report["captures"][0].get("input_sha256") != inputs:
                raise ValueError(f"restored inputs differ from original {method} score: {item['name']}")
        frequency_report_path = (ROOT / "local-evidence" / "triad-extension" / "remote" /
                                 item["reports"] / f"rescore-global-frequency-{item['name']}.json")
        frequency_report = read_json(frequency_report_path)["captures"][0]
        offsets = frequency_report.get("geometry", {}).get("frequency_offsets_hz")
        if not isinstance(offsets, list) or not offsets:
            raise ValueError(f"original public CFO bank missing: {frequency_report_path}")
        allowed_phase_bound = frequency_report.get("frequency_reuse", {}).get(
            "allowed_coherent_power_loss_db")
        if allowed_phase_bound not in (0.01, 0.02):
            raise ValueError(f"unexpected phase approximation bound: {allowed_phase_bound}")
        workspaces.append({**item, "path": workspace, "input_sha256": inputs,
                           "frequency_offsets_hz": offsets,
                           "maximum_block_phase_loss_db": allowed_phase_bound,
                           "original_global_frequency_score": frequency_report_path})
    return source_hash, workspaces


def extract_decision(capture):
    evaluation = capture.get("simulation_evaluation_only", {})
    strict = capture["strict"]
    relaxed = capture["relaxed"]
    truth = evaluation.get("signal_bit")
    control = evaluation.get("control")
    outcomes = evaluation.get("outcomes", {})
    return dict(
        strict=dict(**strict, **outcomes.get("strict", {})),
        relaxed=dict(**relaxed, **outcomes.get("relaxed", {})),
        control=control, truth_bit=truth,
        raw_wrong_rejected=dict(
            strict=(control == "signal" and not strict["accepted"] and truth in (0, 1) and strict["bit"] != truth),
            relaxed=(control == "signal" and not relaxed["accepted"] and truth in (0, 1) and relaxed["bit"] != truth)),
    )


def old_baselines(item):
    root = ROOT / "local-evidence" / "triad-extension" / "remote" / item["reports"]
    result = {}
    for method in BASELINES:
        path = root / f"rescore-{method}-{item['name']}.json"
        report = read_json(path)["captures"][0]
        result[method] = dict(
            path=str(path.relative_to(ROOT)), report_sha256=sha(path),
            receiver_source_sha256=read_json(path).get("receiver_source_sha256"),
            bit_log_likelihoods=report.get("bit_log_likelihoods"),
            strict=report["strict"], relaxed=report["relaxed"],
            decisions=extract_decision(report),
            reblock=report.get("geometry", {}).get("reblock"),
            window_blocks=report.get("geometry", {}).get("window_blocks"),
            nominal_window_seconds=report.get("geometry", {}).get("nominal_window_seconds"),
            frequency_offsets_hz=report.get("geometry", {}).get("frequency_offsets_hz"),
            phase_power_loss_bound_db=report.get("frequency_reuse", {}).get(
                "allowed_coherent_power_loss_db"),
            receiver_seconds=report.get("timing", {}).get("total_receiver_seconds"),
            calibration_alpha=strict_alpha(report),
        )
    return result


def strict_alpha(report):
    strict = report.get("calibration", {}).get("strict", {})
    return strict.get("alpha_per_segment") or strict.get("alpha_per_bit") or strict.get("alpha")


def output_path(job):
    return SCORES / (job["case"] + "--" + job["model"]["name"] + ".json")


def command(job, output):
    item, model = job["workspace_data"], job["model"]
    args = [
        str(PYTHON), str(RECEIVER), str(item["path"]),
        "--doppler-width-hz", repr(model["doppler_width_hz"]),
        "--window-blocks", str(model["window_blocks"]),
        "--reblock", "4",
        "--frequency-offsets-hz", *[repr(x) for x in item["frequency_offsets_hz"]],
        "--maximum-block-phase-loss-db", repr(item["maximum_block_phase_loss_db"]),
        "--group-tile", "8", "--projection-tile-blocks", "256",
        "--memory-mib", "1024", "--device", "cpu", "--precision", "fp32",
        "--threads", "1", "--attempts-per-day", "1440",
        "--duration-alternatives", "8", "--selection-methods", "16",
        "--calibration-segments", "2", "--evaluate-simulation", "--output", str(output),
    ]
    return args


def run_one(job, deadline):
    output = output_path(job)
    model_name = job["model"]["name"]
    status_path = SCORES / (job["case"] + "--" + model_name + ".status.json")
    stdout_path = SCORES / (job["case"] + "--" + model_name + ".stdout.log")
    stderr_path = SCORES / (job["case"] + "--" + model_name + ".stderr.log")
    if any(path.exists() for path in (output, stdout_path, stderr_path)):
        raise FileExistsError(f"refusing to overwrite score output for {job['case']} {job['model']}")
    if status_path.exists():
        prior = read_json(status_path)
        if prior.get("attempted") or not str(prior.get("status", "")).startswith("not-run-"):
            raise FileExistsError(f"refusing to overwrite prior attempted job status: {status_path}")
        status_path.unlink()
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        status = dict(status="not-run-global-deadline", attempted=False,
                      case=job["case"], model=job["model"], command=command(job, output))
        status_path.write_text(json.dumps(status, indent=2, sort_keys=True) + "\n")
        return status
    timeout = min(CHILD_LIMIT_SECONDS, remaining)
    argv = command(job, output)
    env = os.environ.copy()
    env.update(OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1", MKL_NUM_THREADS="1",
               NUMEXPR_NUM_THREADS="1")
    started = time.monotonic()
    try:
        result = subprocess.run(argv, cwd=ROOT, env=env, timeout=timeout,
                                capture_output=True, check=False, text=True, shell=False)
        elapsed = time.monotonic() - started
        stdout_path.write_text(result.stdout)
        stderr_path.write_text(result.stderr)
        status = dict(status="completed" if result.returncode == 0 else "failed",
                      attempted=True, exit_code=result.returncode, elapsed_seconds=elapsed,
                      timeout_seconds=timeout, case=job["case"], model=model_name,
                      command=argv, stdout_path=str(stdout_path.relative_to(ROOT)),
                      stderr_path=str(stderr_path.relative_to(ROOT)))
        if result.returncode == 0:
            if not output.is_file():
                status["status"] = "invalid-no-score-output"
                status["error"] = "receiver exited zero without its requested output"
            else:
                report = read_json(output)
                if report.get("receiver_source_sha256") != job["source_sha256"]:
                    status["status"] = "invalid-receiver-source-hash"
                elif len(report.get("captures", [])) != 1:
                    status["status"] = "invalid-capture-count"
                else:
                    capture = report["captures"][0]
                    if capture.get("input_sha256") != job["workspace_data"]["input_sha256"]:
                        status["status"] = "invalid-input-hash"
                    else:
                        geometry = capture.get("geometry", {})
                        calibration = capture.get("calibration", {})
                        status.update(
                            output_sha256=sha(output),
                            receiver_source_sha256=report["receiver_source_sha256"],
                            input_sha256=capture["input_sha256"],
                            all_declared_groups_scored=(geometry.get("group_count") ==
                                geometry.get("base_group_count") * len(job["workspace_data"]["frequency_offsets_hz"])),
                            calibration=dict(strict=calibration.get("strict"),
                                relaxed=calibration.get("relaxed"),
                                known_noise_variance=calibration.get("known_noise_variance_diagnostic"),
                                selection_methods=calibration.get("strict", {}).get("selection_methods"),
                                attempts_per_day=calibration.get("strict", {}).get("attempts_per_day"),
                                duration_alternatives=calibration.get("strict", {}).get("duration_alternatives")),
                            geometry=geometry,
                            decisions=extract_decision(capture),
                            receiver_timing=capture.get("timing"),
                            resource_array_bound=capture.get("resources"),
                            arithmetic=capture.get("arithmetic"),
                            frequency_reuse=capture.get("frequency_reuse"),
                        )
                        if not status["all_declared_groups_scored"]:
                            status["status"] = "invalid-incomplete-public-bank"
                        elif calibration.get("known_noise_variance_diagnostic") is not None:
                            status["status"] = "invalid-known-noise-mode"
                        elif calibration.get("strict", {}).get("fixed_window_log_threshold") != 22.72106338958574:
                            status["status"] = "invalid-strict-gate"
                        elif calibration.get("relaxed", {}).get("fixed_window_log_threshold") != 17.422746023037703:
                            status["status"] = "invalid-relaxed-gate"
                        else:
                            status["status"] = "complete-valid"
    except subprocess.TimeoutExpired as error:
        elapsed = time.monotonic() - started
        status = dict(status="timeout", attempted=True, elapsed_seconds=elapsed,
                      timeout_seconds=timeout, case=job["case"], model=model_name, command=argv,
                      stdout_tail=str(error.stdout)[-2000:] if error.stdout else "",
                      stderr_tail=str(error.stderr)[-2000:] if error.stderr else "")
        stdout_path.write_text(status["stdout_tail"])
        stderr_path.write_text(status["stderr_tail"])
    with status_path.open("x") as stream:
        json.dump(status, stream, indent=2, sort_keys=True)
        stream.write("\n")
    with _print_lock:
        print(json.dumps({"case": job["case"], "model": job["model"],
                          "status": status["status"], "elapsed_seconds": status.get("elapsed_seconds")}),
              flush=True)
    return status


def compact_outcome(status, category):
    if status.get("status") != "complete-valid":
        return {"status": status.get("status"), "attempted": status.get("attempted")}
    decisions = status["decisions"]
    strict, relaxed = decisions["strict"], decisions["relaxed"]
    if category == "signal":
        return dict(status=status["status"],
            strict_correct=strict.get("correct_admitted", 0),
            strict_wrong=strict.get("wrong_admitted", 0), strict_missed=strict.get("missed", 0),
            strict_raw_wrong_rejected=decisions["raw_wrong_rejected"]["strict"],
            relaxed_correct=relaxed.get("correct_admitted", 0),
            relaxed_wrong=relaxed.get("wrong_admitted", 0), relaxed_missed=relaxed.get("missed", 0),
            relaxed_raw_wrong_rejected=decisions["raw_wrong_rejected"]["relaxed"],
            strict_score=strict.get("score"), relaxed_score=relaxed.get("score"),
            receiver_seconds=status["receiver_timing"].get("total_receiver_seconds"),
            wall_seconds=status.get("elapsed_seconds"),
            maximum_array_bound_bytes=status["resource_array_bound"].get("explicit_array_bound_bytes"))
    return dict(status=status["status"],
        strict_false_admitted=strict.get("false_admitted", 0),
        strict_not_admitted=strict.get("not_admitted", 0),
        relaxed_false_admitted=relaxed.get("false_admitted", 0),
        relaxed_not_admitted=relaxed.get("not_admitted", 0),
        strict_score=strict.get("score"), relaxed_score=relaxed.get("score"),
        receiver_seconds=status["receiver_timing"].get("total_receiver_seconds"),
        wall_seconds=status.get("elapsed_seconds"),
        maximum_array_bound_bytes=status["resource_array_bound"].get("explicit_array_bound_bytes"))


def main():
    ensure_fresh()
    SCORES.mkdir(parents=True, exist_ok=True)
    source_hash, workspaces = preflight()
    pilot_case = next(item for item in workspaces if item["name"] == "none-am65-validation-40-b0-null")
    pilot_model = next(model for model in MODELS if model["window_blocks"] == 4 and model["doppler_width_hz"] == 0.)
    jobs = []
    for item in workspaces:
        for model in MODELS:
            jobs.append(dict(case=item["name"], workspace_data=item, model=model,
                             source_sha256=source_hash))
    pilot = next(job for job in jobs if job["case"] == pilot_case["name"] and
                 job["model"] == pilot_model)
    started_utc = dt.datetime.now(dt.timezone.utc).isoformat()
    start = time.monotonic()
    deadline = start + LIMIT_SECONDS
    # The pilot is one of the thirty planned jobs; its result is retained/reused.
    pilot_status_path = SCORES / (pilot["case"] + "--" + pilot["model"]["name"] + ".status.json")
    if pilot_status_path.is_file():
        # Revalidate the retained pilot after correcting a harness-only field-name mismatch.
        retained = read_json(output_path(pilot))
        capture = retained["captures"][0]
        calibration = capture["calibration"]
        if (retained.get("receiver_source_sha256") != source_hash or
            capture.get("input_sha256") != pilot["workspace_data"]["input_sha256"] or
            calibration.get("known_noise_variance_diagnostic") is not None or
            calibration["strict"].get("fixed_window_log_threshold") != 22.72106338958574 or
            calibration["relaxed"].get("fixed_window_log_threshold") != 17.422746023037703 or
            calibration.get("group_count") != calibration.get("base_group_count") * len(pilot["workspace_data"]["frequency_offsets_hz"])):
            raise ValueError("retained pilot does not satisfy original input, bank and calibration constraints")
        status = read_json(pilot_status_path)
        status.update(status="complete-valid", model=pilot["model"]["name"])
        status["calibration"].update(
            strict_log_threshold=calibration["strict"]["fixed_window_log_threshold"],
            relaxed_log_threshold=calibration["relaxed"]["fixed_window_log_threshold"],
            attempts_per_day=1440, duration_alternatives=8, selection_methods=16)
        pilot_status_path.write_text(json.dumps(status, indent=2, sort_keys=True) + "\n")
        statuses = [status]
    else:
        statuses = [run_one(pilot, deadline)]
    if statuses[0].get("status") != "complete-valid":
        remaining = [job for job in jobs if job is not pilot]
        for job in remaining:
            path = SCORES / (job["case"] + "--" + job["model"]["name"] + ".status.json")
            if not path.exists():
                path.write_text(json.dumps(dict(status="not-run-pilot-failed", attempted=False,
                    case=job["case"], model=job["model"]["name"]), indent=2, sort_keys=True) + "\n")
        statuses.extend(read_json(SCORES / (job["case"] + "--" + job["model"]["name"] + ".status.json"))
                        for job in remaining)
    else:
        remaining = [job for job in jobs if job is not pilot]
        with concurrent.futures.ThreadPoolExecutor(max_workers=MAX_PARALLEL) as pool:
            active = {}
            queue = iter(remaining)
            exhausted = False
            while active or not exhausted:
                while len(active) < MAX_PARALLEL and not exhausted:
                    if time.monotonic() >= deadline:
                        break
                    try:
                        job = next(queue)
                    except StopIteration:
                        exhausted = True
                        break
                    active[pool.submit(run_one, job, deadline)] = job
                if not active:
                    break
                done, _ = concurrent.futures.wait(active, timeout=.2,
                    return_when=concurrent.futures.FIRST_COMPLETED)
                for future in done:
                    job = active.pop(future)
                    statuses.append(future.result())
                if time.monotonic() >= deadline:
                    # Child timeouts are clamped to the same global deadline.
                    for job in list(queue):
                        status_path = SCORES / (job["case"] + "--" + job["model"]["name"] + ".status.json")
                        if not status_path.exists():
                            status_path.write_text(json.dumps(dict(status="not-run-global-deadline",
                                attempted=False, case=job["case"], model=job["model"]["name"]),
                                indent=2, sort_keys=True) + "\n")
                    exhausted = True
            statuses.extend(read_json(p) for p in SCORES.glob("*.status.json")
                            if not any(s.get("case") == read_json(p).get("case") and
                                       s.get("model") == read_json(p).get("model") for s in statuses))

    by_job = {(status.get("case"), status.get("model")): status for status in statuses}
    summary_cases = []
    for item in workspaces:
        baseline = old_baselines(item)
        case_status = []
        for model in MODELS:
            status = by_job.get((item["name"], model["name"]))
            if status is None:
                status = read_json(SCORES / (item["name"] + "--" + model["name"] + ".status.json"))
            case_status.append(dict(model=dict(
                doppler_width_hz=model["doppler_width_hz"], reblock=4,
                original_block_seconds=128/4096, window_blocks=model["window_blocks"],
                nominal_window_seconds=model["window_seconds"]),
                outcome=compact_outcome(status,
                    "signal" if item["name"].endswith("signal") else "control"),
                status_path=str((SCORES / (item["name"] + "--" + model["name"] + ".status.json")).relative_to(ROOT))))
        summary_cases.append(dict(case=item["name"], control=read_json(item["path"] / "manifest.json")["control"],
            cn0_db_hz=read_json(item["path"] / "manifest.json")["cn0_db_hz"],
            workspace_relative_path=str(item["path"].relative_to(ROOT)),
            workspace_input_sha256=item["input_sha256"],
            original_public_frequency_offsets_hz=item["frequency_offsets_hz"],
            original_phase_loss_bound_db=item["maximum_block_phase_loss_db"],
            original_results=baseline, intermediate_models=case_status))

    complete = sum(s.get("status") == "complete-valid" for s in statuses)
    summary = dict(
        schema="triad-continuity-intermediate-screen-v1",
        status="complete" if complete == len(jobs) else "partial",
        started_utc=started_utc, elapsed_seconds=time.monotonic()-start,
        receiver_source_sha256=source_hash,
        user_requested_model_choices=len(MODELS),
        model_families_in_allocation=9,
        fixed_multiple_testing_calibration=dict(attempts_per_day=1440,
            duration_alternatives=8,selection_methods=16,known_noise_variance=None,
            strict_log_threshold=22.72106338958574,relaxed_log_threshold=17.422746023037703,
            unknown_noise_upper_confidence=True,
            note="Six intermediate choices are evaluated on same paths as three retained original methods; all six public model alternatives and all declared CFO/timing groups enter mixture/calibration."),
        execution_limits=dict(child_timeout_seconds=CHILD_LIMIT_SECONDS,
            global_timeout_seconds=LIMIT_SECONDS,max_parallel_jobs=MAX_PARALLEL,
            threads_per_child=1,group_tile=8,memory_limit_mib=1024,precision="fp32"),
        intermediate_design=dict(reblock=4, original_group_block_seconds=128/4096,
            resulting_base_block_seconds=4*128/4096,
            window_blocks=[4,16,32],window_seconds=[.5,2.,4.],
            doppler_width_hz=[0.,.2],cfo_offsets_source="unmodified original global-frequency report per capture",
            phase_loss_bounds_source="unmodified original global-frequency report per capture; microwave arm preserves its 0.02 dB bound"),
        outcome_counts=dict(attempted=len([s for s in statuses if s.get("attempted")]),
            completed_valid=complete,failed=sum(s.get("attempted") and s.get("status") != "complete-valid" for s in statuses),
            not_run=sum(not s.get("attempted") for s in statuses)),
        cases=summary_cases,
        inference_limits=[
            "Post-failure paired screen on five original channel/noise captures; SNR, bit/control, CFO grid and physical paths are reused.",
            "Reblocking changes the original block-center likelihood approximation as well as the integration window; it is not a pure window-gain comparison.",
            "Independent Gaussian coefficient windows are an alternative channel likelihood, not a full sampled selective-channel likelihood.",
            "No receiver or threshold is retuned, and the three original score reports are read from compact evidence without rerunning them.",
            "Five captures and one null/two wrong-key controls are diagnostic counts, not independent false-admission qualification.",
            "CPU peak RSS was not measured per job; explicit scorer array bounds and elapsed costs are retained.",
        ],
    )
    out = LOCAL / "summary.json"
    with out.open("x") as stream:
        json.dump(summary, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(json.dumps(dict(status=summary["status"], elapsed_seconds=summary["elapsed_seconds"],
        attempted=summary["outcome_counts"]["attempted"],completed_valid=complete,
        failed=summary["outcome_counts"]["failed"],not_run=summary["outcome_counts"]["not_run"],
        summary=str(out.relative_to(ROOT))), sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
