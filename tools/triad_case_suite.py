"""Shared configuration and provenance helpers for Robust manual cases."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

from triad_cases import DEFAULT_CASES, ROOT, load_cases


def identity(executable):
    paths = sorted(set([*ROOT.glob("tools/triad*.*"), *ROOT.glob("src/triad*.*"),
                        *ROOT.glob("include/datapump/triad*.*"),
                        ROOT / "src/compression_short.cpp", ROOT / "src/tuning_presets.cpp",
                        ROOT / "src/helper_carrier.cpp"]))
    return {
        "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "dirty_paths": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True).splitlines(),
        "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in paths if p.is_file()},
        "native_binary": str(executable),
        "native_binary_sha256": hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
    }


def native_plan(executable, native_args):
    command = [str(executable), *native_args, "--seconds", ".001", "--sample-rate", "6000", "--data-if", "1725",
               "--epoch", "1793850000", "--key-seed", "72001", "--short-message", "fox", "--plan-only"]
    return json.loads(subprocess.check_output(command, text=True, timeout=30))


def target_snr(plan, complete_admission=None):
    """Invert a matched complete-admission model to propose, never qualify, a target."""
    from triad_duration_model import plan_from_native
    if complete_admission is None:
        return {"status": "unavailable", "reason": "matched complete-admission receipt is required"}
    if not plan.get("geometry_feasible"):
        return {"status": "unavailable", "reason": "native geometry infeasible"}
    duration = float(plan["geometry"]["active_data_seconds"])
    bits = int(plan["short_message"]["encoded_bits"])
    requested = .91

    def prediction(cn0, message_bits):
        return plan_from_native(plan, cn0, correct_probability=requested,
                                message_bits=message_bits, manual_seconds=duration,
                                max_active_seconds=max(duration * 2, 1), max_evaluations=4,
                                complete_admission=complete_admission)

    estimates = {}
    for count, name in ((1, "single_bit"), (bits, "fox")):
        low, high = -100., 100.
        upper = prediction(high, count)
        if not (upper.get("selected") or {}).get("meets_detection_allocation", False):
            return {"status": "unavailable", "reason": "duration model target exceeds declared SNR bracket",
                    "searched_bracket_db_hz": [low, high], "source_bits": count}
        for _ in range(24):
            mid = (low + high) / 2
            estimate = prediction(mid, count)
            if not estimate.get("selected"):
                return {"status": "unavailable", "reason": "duration model did not produce a selected duration",
                        "searched_bracket_db_hz": [low, high], "source_bits": count}
            if estimate["selected"]["meets_detection_allocation"]:
                high = mid
            else:
                low = mid
        estimate = prediction(high, count)
        estimates[name] = {"target_cn0_db_hz": high, "bracket_db_hz": [low, high],
                           "source_bits": count,
                           "modeled_true_atom_detection_lower_bound": estimate["selected"]["modeled_detection_lower_bound"],
                           "unmodeled_effects": estimate["unmodeled_selected_effects"],
                           "assumptions": estimate["assumptions"]}
    return {"status": "provisional_model_target", "requested_message_success_probability": requested,
            "whole_bit_or_message_probability_demonstrated": False,
            "scope": "Candidate target from the matched complete-admission model; acquisition retention, Helper and wrong-bit errors are not qualified. Not a measured >90% threshold.",
            "complete_admission_receipt": complete_admission,
            "data_duration_per_bit_seconds": duration, **estimates}


def write_result(path, result):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def select_cases(parsed, indices=None):
    """Select explicit current-file row numbers; the default selects all."""
    if indices is None:
        return parsed["cases"]
    wanted = set(indices)
    available = {case["index"] for case in parsed["cases"]}
    if not wanted or any(isinstance(i, bool) or not isinstance(i, int) for i in wanted) or not wanted <= available:
        raise ValueError("selected case indices must exist in the freshly parsed list")
    return [case for case in parsed["cases"] if case["index"] in wanted]


def run_cases(*, executable, output, case_list=DEFAULT_CASES, measurement_seconds=1., threads=None,
              case_indices=None, paired_followup=False, message_gate=True):
    """Compatibility wrapper for the dedicated absolute-capacity runner."""
    from triad_case_capacity import run_complete_cases
    return run_complete_cases(
        executable=executable,
        output=output,
        case_list=case_list,
        measurement_seconds=measurement_seconds,
        threads=threads,
        case_indices=case_indices,
        paired_followup=paired_followup,
        message_gate=message_gate,
    )
