#!/usr/bin/env python3
"""Measure absolute CPU and workspace capacity for the editable Robust cases.

This manual-case suite uses bounded, efficient samples to estimate absolute
single-receiver cost and conditional success for the current configurations.
It does not measure degradation against an earlier build. Complete receiver
work, public state ceilings, quiet completion and unresolved evidence are
reported separately; no omitted stage can produce a capacity pass.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CASES = ROOT / "_tests-RobustModem.md"
DEFAULT_EXPORTER = ROOT / "build/dev/triad_helper_export"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description=(
            "Absolute local CPU and workspace capacity measurements for the "
            "editable Robust manual cases. Uses bounded abbreviated sampling; "
            "this is not the kernel degradation regression suite."
        ),
        epilog=(
            "The current manual-case receiver coverage is recorded in the "
            "report. Incomplete receiver stages remain unresolved, not passed. "
            "Run the separate kernel regression with triad_abbreviated.py."
        ),
    )
    parser.add_argument(
        "--case-list", type=Path, default=DEFAULT_CASES,
        help="manual configuration source, reread and strictly validated on each run",
    )
    parser.add_argument("--output", type=Path, required=True,
                        help="JSON result path; full capacity runs also write a matching Markdown report")
    parser.add_argument("--exporter", type=Path, default=DEFAULT_EXPORTER,
                        help="native Robust waveform/template exporter")
    parser.add_argument("--measurement-seconds", "--probe-seconds", dest="measurement_seconds",
                        type=float, default=1.,
                        help="bounded streaming measurement input, from 0.1 to 2 seconds")
    parser.add_argument("--threads", type=int,
                        help="explicit single receiver-worker allocation for debugging; default compares the supported local allocations")
    parser.add_argument('--full-logical-inference', action=argparse.BooleanOptionalAction,default=None,
                        help='default to all visible logical CPUs after the pilot; opt out to use the pilot winner')
    parser.add_argument("--case-index", type=int, action="append",
                        help="select a current manual row for explicitly partial debugging")
    parser.add_argument('--calibration-stage',choices=('noise-only-fine',),
                        help='measure only the selected H0 fine cost class for a GPU forecast')
    parser.add_argument("--skip-message-gate", action="store_true",
                        help="debug measurements without the current-source native message gate; prevents an integrated suite pass")
    parser.add_argument('--sustained-allocation-probe', action='store_true',
                        help='bounded exploratory continued fine work at physical and full logical allocations')
    parser.add_argument('--workspace-mib', type=int,
                        help='stable workspace ceiling within the visible local receiver budget')
    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    # Keep the statistical policy, reporting, and implementation in one place;
    # this entry point does not make a separate copy of the manual suite.
    options = {}
    if args.calibration_stage is None:
        from triad_case_suite import run_cases
    else:
        if (not args.case_index or args.workspace_mib is None or args.threads is not None or
                args.sustained_allocation_probe or args.full_logical_inference is False):
            parser.error('stage calibration requires selected rows, a fixed workspace and full logical workers')
        from triad_case_capacity import run_complete_cases as run_cases
        options['calibration_stage'] = args.calibration_stage

    result = run_cases(
        executable=args.exporter,
        output=args.output,
        case_list=args.case_list,
        measurement_seconds=args.measurement_seconds,
        threads=args.threads,
        full_logical_inference=args.full_logical_inference,
        case_indices=args.case_index,
        message_gate=not args.skip_message_gate,
        sustained_allocation_probe=args.sustained_allocation_probe,
        workspace_mib=args.workspace_mib,
        **options,
    )
    return (0 if result.get('status') in ('conditional_capacity_pass','stage_calibration_measured') else
            1 if result.get('status') == 'capacity_failures' else 2)


if __name__ == "__main__":
    sys.exit(main())
