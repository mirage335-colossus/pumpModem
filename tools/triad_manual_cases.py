#!/usr/bin/env python3
"""Measure absolute local CPU and workspace capacity for the editable Robust cases."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

from triad_cases import DEFAULT_CASES
from triad_native import DEFAULT_PROBE, write_result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Absolute local CPU and workspace capacity measurements for the editable Robust manual cases. "
                    "Uses bounded native sampling and is not the kernel degradation regression suite.",
        epilog="Use triad_abbreviated.py for relative native regressions. Capacity passes require complete native work.",
    )
    parser.add_argument("--case-list", type=Path, default=DEFAULT_CASES,
                        help="Read and strictly validate the entire current list before selecting rows.")
    parser.add_argument("--output", type=Path, required=True,
                        help="Native JSON result. Capacity runs also write a Markdown summary.")
    parser.add_argument("--probe", "--exporter", type=Path, dest="probe", default=DEFAULT_PROBE,
                        help="Compiled receiver probe. The legacy exporter option selects its sibling probe.")
    parser.add_argument("--measurement-seconds", "--probe-seconds", type=float, default=1.,
                        help="Reference calibration unit, from 0.1 to 2 seconds. Native physical geometry remains fixed.")
    parser.add_argument("--threads", type=int, help="Exact positive worker override. Omit for automatic 2H.")
    parser.add_argument("--slot-owners", type=int, help="Exact native slot-owner limit.")
    parser.add_argument("--full-logical-inference", action=argparse.BooleanOptionalAction, default=None,
                        help="Explicitly use H for inference, or the measured pilot winner with --no-full-logical-inference.")
    parser.add_argument("--case-index", type=int, action="append", help="Select current rows for explicitly partial work.")
    parser.add_argument("--reuse-identical", action="store_true",
                        help="Reuse identical normalized inputs and mark each reused row.")
    parser.add_argument("--calibration-stage", choices=("noise-only-fine",),
                        help="Collect a selected native noise-only calibration phase.")
    parser.add_argument("--skip-message-gate", action="store_true",
                        help="Diagnostic only. Prevents an integrated suite pass.")
    parser.add_argument("--sustained-allocation-probe", action="store_true",
                        help="Add bounded continued fine work at physical and full logical allocations.")
    parser.add_argument("--workspace-mib", type=int, help="Exact host workspace ceiling in MiB.")
    parser.add_argument("--deadline-ms", type=int, default=30_000, help="Maximum elapsed time per native call.")
    parser.add_argument("--row-deadline-ms", type=int, default=180_000, help="Maximum elapsed time for each row.")
    parser.add_argument("--max-samples", type=int, default=4_194_304, help="Maximum generated samples per native call.")
    parser.add_argument("--coarse-tiles", type=int, default=4096, help="Maximum fixed-grid coarse tiles per native call.")
    parser.add_argument("--backlog-slots", type=int,
                        help="Exact public slots per replay. Default selects a bounded horizon from the native plan.")
    parser.add_argument("--stream-looks", type=int, choices=(1, 3, 5, 9), default=1,
                        help="Predeclared whole-stream observations. Default records one bounded point.")
    parser.add_argument("--max-spool-bytes", type=int, help="Exact temporary native capture byte cap.")
    parser.add_argument("--target-cn0", type=float, help="Public duration-model target, default 32 dB-Hz.")
    parser.add_argument("--seconds", type=float, help="Explicit public DATA duration for a separately declared geometry.")
    parser.add_argument("--sensitivity", action="store_true",
                        help="Separate independent complete-receiver trials for explicitly selected rows.")
    parser.add_argument("--sensitivity-cn0", type=float, help="Actual trial C/N0 in dB-Hz. Omit to use the native model target.")
    parser.add_argument("--sensitivity-trials", type=int, default=3, help="Predeclared independent trial count, up to 29.")
    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.calibration_stage is not None and (not args.case_index or args.workspace_mib is None
            or args.threads is not None or args.sustained_allocation_probe or args.full_logical_inference is False):
        parser.error("Stage calibration requires selected rows, fixed workspace and automatic or full-logical allocation")
    if args.sensitivity and (args.calibration_stage or args.skip_message_gate or args.reuse_identical
            or args.sustained_allocation_probe or args.full_logical_inference is not None
            or args.target_cn0 is not None or args.seconds is not None or args.backlog_slots is not None
            or args.stream_looks != 1 or args.max_spool_bytes is not None):
        parser.error("Sensitivity trials use a separate native measurement policy")
    if not args.sensitivity and (args.sensitivity_cn0 is not None or args.sensitivity_trials != 3):
        parser.error("Sensitivity options require --sensitivity")
    from triad_case_suite import SCHEMA, SENSITIVITY_SCHEMA, run_cases, run_sensitivity
    options = {"executable": args.probe, "output": args.output, "case_list": args.case_list,
               "threads": args.threads, "case_indices": args.case_index, "workspace_mib": args.workspace_mib}
    for name, default in (("slot_owners", None), ("deadline_ms", 30_000), ("row_deadline_ms", 180_000),
                          ("max_samples", 4_194_304), ("coarse_tiles", 4096)):
        value = getattr(args, name)
        if value != default:
            options[name] = value
    try:
        if args.sensitivity:
            result = run_sensitivity(**options, cn0=args.sensitivity_cn0, trials=args.sensitivity_trials)
        else:
            if args.calibration_stage:
                options["calibration_stage"] = args.calibration_stage
            if args.reuse_identical:
                options["reuse_identical"] = True
            if args.target_cn0 is not None:
                options["target_cn0"] = args.target_cn0
            if args.seconds is not None:
                options["fixed_active_seconds"] = args.seconds
            if args.backlog_slots is not None:
                options["backlog_slots"] = args.backlog_slots
            if args.stream_looks != 1:
                options["stream_looks"] = args.stream_looks
            if args.max_spool_bytes is not None:
                options["max_spool_bytes"] = args.max_spool_bytes
            result = run_cases(**options, measurement_seconds=args.measurement_seconds,
                               full_logical_inference=args.full_logical_inference,
                               message_gate=not args.skip_message_gate,
                               sustained_allocation_probe=args.sustained_allocation_probe)
    except (ValueError, OSError) as error:
        result = {"schema": SENSITIVITY_SCHEMA if args.sensitivity else SCHEMA,
                  "status": "error", "reason": str(error)}
        write_result(args.output, result)
        print(result["reason"], file=sys.stderr)
    status = result.get("status")
    if status in ("conditional_capacity_pass", "stage_calibration_measured", "measured_independent_trials"):
        return 0
    return 1 if status in ("capacity_failures", "error") else 2


if __name__ == "__main__":
    sys.exit(main())
