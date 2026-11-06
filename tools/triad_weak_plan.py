#!/usr/bin/env python3
"""Plan a provisional weak-receiver DATA duration and optionally export one bit.

This freezes a Gaussian true-atom duration forecast and native exporter geometry;
it does not run a receiver or qualify correct reception or a physical null.

Example::

  python tools/triad_weak_plan.py --target-cn0 8 --output local-evidence/weak-one \
    -- --case hf --sample-rate 6000 --cn0 8
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from triad_duration_model import plan_from_native  # noqa: E402
from triad_operational_admission import public_grid  # noqa: E402

RESERVED = {'--seconds', '--plan-only', '--output', '--whole-bit-count',
            '--whole-bit-wait-count', '--templates'}
MAX_STDOUT = 2_000_000


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def native_flags(args):
    flags = set()
    for arg in args:
        if arg.startswith('--'):
            flags.add(arg.split('=', 1)[0])
    return flags


def run_json(command, *, cwd):
    started = time.time()
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False, timeout=60)
    elapsed = time.time() - started
    if result.returncode:
        raise RuntimeError(f'native command failed ({result.returncode}): {result.stderr[-4000:]}')
    if len(result.stdout.encode()) > MAX_STDOUT:
        raise RuntimeError('native JSON output exceeds bounded capture limit')
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f'native command did not emit JSON: {exc}') from exc
    return value, dict(command=[str(x) for x in command], elapsed_seconds=elapsed,
                       returncode=result.returncode, stdout=result.stdout,
                       stderr=result.stderr[-4000:])


def _parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--exporter', type=Path, default=ROOT / 'build/dev-cli/triad_helper_export')
    p.add_argument('--target-cn0', type=float, help='nominal DATA C/N0 used for automatic duration selection')
    p.add_argument('--correct-probability', type=float, default=.99)
    p.add_argument('--message-bits', type=int, default=1)
    p.add_argument('--alpha-per-day', type=float, default=1e-4)
    p.add_argument('--attempt-spacing-seconds', type=float, default=.4)
    p.add_argument('--duration-alternatives', type=int, default=1)
    p.add_argument('--outage-fraction', type=float, default=0.)
    p.add_argument('--retained-fraction', type=float, default=1.)
    p.add_argument('--max-active-seconds', type=float, default=315576000.)
    p.add_argument('--seconds', type=float, help='manual active-duration override; rounded up to native tiles')
    p.add_argument('--output', type=Path, help='optional new capture directory; omit to write plan only')
    p.add_argument('--report', type=Path, help='plan JSON path (defaults beside output, or ./triad-weak-plan.json)')
    p.add_argument('--templates', action='store_true', help='include the native all-epoch template bank in the export')
    return p


def _check_plan_only(native, seconds, planned):
    fmt = native.get('format', {})
    selected = planned.get('selected') or {}
    expected_tiles = selected.get('tiles')
    if expected_tiles is None:
        return
    if int(fmt.get('tile_count', -1)) != int(expected_tiles):
        raise ValueError('native selected-duration tile count differs from frozen plan')
    reported = fmt.get('bit_seconds', fmt.get('elapsed_bit_seconds', -1))
    if not math.isclose(float(reported), float(selected.get('elapsed_data_seconds', seconds)), rel_tol=0, abs_tol=1e-8):
        raise ValueError('native selected-duration elapsed bit time differs from frozen calendar')


def execute(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if '--' in argv:
        split = argv.index('--')
        wrapper_argv, native_args = argv[:split], argv[split + 1:]
    else:
        wrapper_argv, native_args = argv, []
    args = _parser().parse_args(wrapper_argv)
    if args.output is not None:
        args.output = args.output.expanduser().resolve()
    if args.report is not None:
        args.report = args.report.expanduser().resolve()
    conflicts = native_flags(native_args) & RESERVED
    if conflicts:
        raise ValueError('wrapper controls reserved native options: ' + ', '.join(sorted(conflicts)))
    if not native_args:
        raise ValueError('native exporter arguments are required after -- (include --case hf|eme)')
    for index, arg in enumerate(native_args[:-1]):
        if arg == '--power-reference' and native_args[index+1] != 'whole-active':
            raise ValueError('duration energy accounting requires --power-reference whole-active')
        if arg == '--control' and native_args[index+1] != 'selected':
            raise ValueError('use explicit None/Off channel parameters instead of a flat diagnostic bypass')
    if args.target_cn0 is None and args.seconds is None:
        raise ValueError('--target-cn0 is required for automatic duration planning')
    if args.output is not None and args.output.exists():
        raise FileExistsError(f'export output already exists: {args.output}')
    report_path = args.report or ((args.output.parent / (args.output.name + '-duration-plan.json'))
                                  if args.output else Path('triad-weak-plan.json'))
    if report_path.exists():
        raise FileExistsError(f'plan report already exists: {report_path}')
    exporter = args.exporter.expanduser().resolve()
    if not exporter.is_file():
        raise FileNotFoundError(f'native exporter does not exist: {exporter}')
    native_base = [str(exporter), *native_args]
    probe, probe_run = run_json([*native_base, '--seconds', '8', '--plan-only'], cwd=ROOT)
    # The historical native shaping report does not expose its Helper on/off flag.
    # Resolve the same last-option-wins argv setting rather than inferring it from power.
    helper_enabled = True
    for index, arg in enumerate(native_args[:-1]):
        if arg == '--helper':
            helper_enabled = native_args[index+1] == 'on'
    probe['helper_enabled'] = helper_enabled
    if probe.get('schema') != 'triad-helper-shaping-plan-v1' or 'format' not in probe:
        raise ValueError('native probe lacks the supported triad shaping-plan geometry')
    target = (float(probe['physics']['data_cn0_db_hz']) if args.target_cn0 is None
              else args.target_cn0)
    planned = plan_from_native(probe, target, correct_probability=args.correct_probability,
        message_bits=args.message_bits, alpha_per_day=args.alpha_per_day,
        attempt_spacing_seconds=args.attempt_spacing_seconds,
        duration_alternatives=args.duration_alternatives, outage_fraction=args.outage_fraction,
        retained_fraction=args.retained_fraction, max_active_seconds=args.max_active_seconds,
        manual_seconds=args.seconds)
    chosen = planned.get('selected')
    selected_probe = None
    selected_run = None
    if chosen is not None and float(planned['active_seconds']) <= 3600:
        selected_probe, selected_run = run_json(
            [*native_base, '--seconds', format(float(planned['active_seconds']), '.17g'), '--plan-only'], cwd=ROOT)
        _check_plan_only(selected_probe, planned['active_seconds'], planned)
    report = dict(schema='triad-weak-plan-export-v1', created_unix_seconds=time.time(),
        duration_plan=planned,
        target_cn0_supplied=args.target_cn0 is not None,
        target_cn0_forecast_source=('native probe physics.data_cn0_db_hz (physical-noise forecast; may include native --cn0 override)'
                                   if args.target_cn0 is None else '--target-cn0'),
        weak_admission_policy=dict(planned['admission_policy']),
        weak_admission_policy_applied=False,
        native=dict(exporter=str(exporter), exporter_sha256=sha256(exporter),
            source_sha256={str(path.relative_to(ROOT)): sha256(path) for path in (
                ROOT / 'tools/triad-helper-export.cpp', ROOT / 'tools/triad_duration_model.py',
                ROOT / 'tools/triad_weak_plan.py')},
            probe=probe_run, probe_plan=probe,
            selected_duration_probe=selected_run, selected_duration_plan=selected_probe,
            selected_duration_probe_status=('verified' if selected_probe is not None else
                'unavailable: native exporter caps plan-only duration at 3600 seconds' if chosen is not None else 'no selected duration')),
        export=None, report_path=str(report_path))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open('x', encoding='utf-8') as f:
        json.dump(report, f, indent=2, allow_nan=False)
        f.write('\n')
    if args.output is None:
        return report
    if not planned.get('export_supported') or chosen is None:
        raise RuntimeError('requested export unavailable; retained plan status: '+planned['status'])
    seconds = float(planned['active_seconds'])
    # The sidecar already contains the selected-duration geometry revalidation.
    command = [*native_base, '--seconds', format(seconds, '.17g'), '--whole-bit-count', '1',
               '--output', str(args.output)]
    if args.templates:
        command.append('--templates')
    export_started = time.time()
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False, timeout=300)
    if completed.returncode:
        report['export'] = dict(command=command, returncode=completed.returncode,
                                stderr=completed.stderr[-4000:], elapsed_seconds=time.time()-export_started)
        with report_path.open('w', encoding='utf-8') as f:
            json.dump(report, f, indent=2, allow_nan=False); f.write('\n')
        raise RuntimeError(f'native export failed ({completed.returncode}): {completed.stderr[-4000:]}')
    manifest_path = args.output / 'manifest.json'
    if not manifest_path.is_file():
        raise ValueError('native exporter completed without manifest.json')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    fmt = manifest['format']
    if int(fmt['tile_count']) != int(chosen['tiles']) or not math.isclose(float(fmt['bit_seconds']), float(chosen['elapsed_data_seconds']), rel_tol=0, abs_tol=1e-8):
        raise ValueError('export manifest duration differs from frozen plan')
    reference_active = manifest.get('power', {}).get('data_reference_active_seconds')
    if reference_active is not None and not math.isclose(float(reference_active), float(planned['active_seconds']), rel_tol=0, abs_tol=1e-7):
        raise ValueError('export power reference active duration differs from frozen plan')
    fs = manifest['sample_rate_hz']
    slot = manifest['whole_bit_calendar']['slots'][0]
    origin = (int(slot['helper_begin_sample'])-math.ceil(fs))/fs
    drift = origin*manifest['channel']['sample_clock_pair_ppm']*manifest['channel']['spread_margin']*1e-6
    lo, hi = manifest['calendar']['arrival_prior_seconds']
    grid = public_grid(manifest['channel'], (lo-drift, hi+drift), tuple(range(-6, 7)))
    if grid.candidate_count > int(planned['full_grid_count']):
        raise ValueError('export public grid exceeds the full grid charged by the duration plan')
    native_manifest_sha256 = sha256(manifest_path)
    manifest['weak_admission_policy'] = dict(planned['admission_policy'])
    with manifest_path.open('w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2, allow_nan=False)
        f.write('\n')
    final_manifest_sha256 = sha256(manifest_path)
    report['weak_admission_policy_applied'] = True
    report['export'] = dict(command=command, returncode=completed.returncode,
        elapsed_seconds=time.time()-export_started, stdout=completed.stdout[-4000:],
        stderr=completed.stderr[-4000:], manifest_path=str(manifest_path),
        native_manifest_sha256=native_manifest_sha256,
        manifest_sha256=final_manifest_sha256, public_grid_count=grid.candidate_count,
        public_grid_sha256=grid.identity(), templates_requested=args.templates)
    with report_path.open('w', encoding='utf-8') as f:
        json.dump(report, f, indent=2, allow_nan=False); f.write('\n')
    return report


def main():
    try:
        report = execute()
    except (ValueError, OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f'triad_weak_plan: {exc}', file=sys.stderr)
        return 2
    print(json.dumps(dict(report=str(report.get('report_path', 'written')), status=report['duration_plan']['status'],
                          active_seconds=report['duration_plan']['active_seconds'], exported=report['export'] is not None),
                     sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
