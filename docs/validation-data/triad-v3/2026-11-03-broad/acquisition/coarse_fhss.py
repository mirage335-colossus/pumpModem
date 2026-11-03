#!/usr/bin/env python3
"""Freeze a coarser public FHSS timing bank after the original resource failure.

Default: native random-chip/RRC three-carrier3dB grid, full unchanged prior.
An explicit --maximum-step .001 emits a separate resource diagnostic whose
nearest-grid autocorrelation crosses a main-lobe zero; it is not a3dB grid.
No export or receiver job is launched. An optional configure-only probe is
bounded to10seconds and retains its complete native result.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('fhss_parent_plan_helpers', HERE/'prepare.py')
helpers = importlib.util.module_from_spec(spec); spec.loader.exec_module(helpers)
SCRIPT = 'docs/validation-data/triad-v3/2026-11-03-broad/acquisition/coarse_fhss.py'

def correlation_power(delay, fine_rate, spacing, rolloff=.25):
    """Same approximation as native timing_grid_step, including RC removable pole."""
    u = delay*fine_rate
    sinc = math.sin(math.pi*u)/(math.pi*u) if u else 1.
    denominator = 1-4*rolloff*rolloff*u*u
    if abs(denominator) < 1e-12:
        pulse = math.pi/4*math.sin(math.pi/(2*rolloff))/(math.pi/(2*rolloff))
    else:
        pulse = sinc*math.cos(math.pi*rolloff*u)/denominator
    return (pulse*(1+2*math.cos(2*math.pi*spacing*delay))/3)**2

def maximum_step_for_loss(loss_db, fine_rate, spacing):
    if not 0 < loss_db <= 60: raise ValueError('loss budget must be in (0,60]dB')
    low, high = 0., min(1/fine_rate, 1/(3*spacing))
    target = 10**(-loss_db/10)
    for _ in range(80):
        mid = (low+high)/2
        if correlation_power(mid, fine_rate, spacing) >= target: low = mid
        else: high = mid
    return 2*low

def loss_description(step, fine_rate, spacing):
    half = step/2
    zero = min(1/fine_rate, 1/(3*spacing))
    value = correlation_power(half, fine_rate, spacing)
    crosses = half >= zero
    return dict(fine_rate_per_carrier_hz=fine_rate, carrier_spacing_hz=spacing,
        nearest_grid_maximum_offset_seconds=half,
        first_main_lobe_zero_offset_seconds=zero,
        nearest_grid_cell_contains_autocorrelation_zero=crosses,
        worst_nearest_grid_loss_db=None if crosses else -10*math.log10(value),
        worst_nearest_grid_loss_unbounded=crosses,
        half_step_endpoint_loss_db=-10*math.log10(value) if value > 0 else None,
        scope='Native independent random-chip, equal-power triplet, shared-gain infinite-RRC autocorrelation approximation; not a finite-keyed-pattern or multipath certificate. Beyond the first zero no nearest-grid finite loss guarantee is available; other bank sidelobes do not restore this guarantee.')

def build(parent, loss_db, maximum_step):
    result = copy.deepcopy(parent)
    result['cells'] = [c for c in result['cells'] if c['case'] in ('erasures-none', 'erasures-cycle-edge')]
    result['jobs'] = [j for j in result['jobs'] if j['case'] in ('erasures-none', 'erasures-cycle-edge')]
    if len(result['cells']) != 2 or len(result['jobs']) != 18: raise ValueError('expected exactly two FHSS cells and18jobs')
    cells = {}
    for cell in result['cells']:
        original = next(j for j in result['jobs'] if j['case'] == cell['case'] and j['stage'] == 'export')
        argv = original['argv']
        fine = float(helpers.option(argv, '--rate'))*float(helpers.option(argv, '--added'))/6
        spacing = 1.25*fine
        requested = maximum_step if maximum_step is not None else maximum_step_for_loss(loss_db, fine, spacing)
        half = cell['timing_grid'][1]
        if cell['timing_grid'][0] != -half: raise ValueError('parent timing grid must be symmetric')
        grid, groups = helpers.symmetric_grid(half, requested)
        if groups >= cell['groups']: raise ValueError('followup must reduce the original timing bank')
        previous = cell['groups']
        cell.update(timing_grid=grid, groups=groups, nominal_group_index=(groups-1)//2,
            timing_loss_budget_db=loss_db if maximum_step is None else None,
            timing_step_recommendation_seconds=requested,
            timing_loss_scope=f'{loss_db:g}dB native approximation' if maximum_step is None else 'Explicit coarse resource diagnostic; inspect first-zero violation',
            prior_group_count=previous, predicted_group_work_reduction=previous/groups,
            timing_loss_analysis=loss_description(grid[2], fine, spacing))
        cells[cell['case']] = cell
    for value in result['jobs']:
        cell = cells[value['case']]
        value.update(expected_groups=cell['groups'], nominal_group_index=cell['nominal_group_index'])
        if value['stage'] in ('configure', 'export'):
            before = list(value['argv'])
            helpers.replace(value['argv'], '--start', *cell['timing_grid'])
            # Physical, sampling, block, C/N0, erasure, key and seed arguments
            # must remain byte-for-byte unchanged outside this public bank.
            first = before.index('--start')
            check = list(value['argv']); check[first:first+4] = before[first:first+4]
            if check != before: raise ValueError('unexpected physical argument mutation')
        if value['stage'] == 'export': value['timeout_seconds'] = 300
        value['definition_sha256'] = helpers.digest({k:v for k,v in value.items() if k != 'definition_sha256'})
    result['limits']['maximum_child_seconds'] = 300
    result['recommended_stage_bound_seconds'] = 1200
    result['recommended_concurrent_exports'] = 3
    result['selection_reason'] = 'All six original1077-group FHSS exports exhausted their180-second resource bounds. This explicitly changed grid and300-second bound are a resource-resolution followup, not an implicit retry or independent validation.'
    result['coarse_grid_mode'] = f'native-{loss_db:g}db' if maximum_step is None else 'explicit-maximum-step-diagnostic'
    result['one_millisecond_hypothesis_check'] = loss_description(
        helpers.symmetric_grid(.090500000001, .001)[0][2], 600., 750.)
    result['limitations'].extend([
        'The complete original arrival interval remains searched; coarsening trades expected timing mismatch against cost without using sampled truth.',
        'Predicted work reduction refers to group-proportional work only. Actual export latency must be measured with a pilot; source/channel work and I/O do not necessarily scale by this factor.',
        'The proposed1ms maximum step is not approximately3dB for Added10: its nearest-grid cell includes a triplet autocorrelation zero. The default follows the native3dB main-lobe bound instead.',
        'Use a fresh output root and run directories. Old timeout receipts remain distinct and are not inputs to this changed plan.'])
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    group = p.add_mutually_exclusive_group()
    group.add_argument('--loss-db', type=float, default=3.)
    group.add_argument('--maximum-step', type=float)
    p.add_argument('--probe-binary', type=Path)
    p.add_argument('--probe-output', type=Path)
    args = p.parse_args()
    if args.output.exists() or (args.probe_output and args.probe_output.exists()): p.error('outputs must be fresh')
    if (args.probe_binary is None) != (args.probe_output is None): p.error('probe binary/output must be supplied together')
    if args.maximum_step is not None and (not math.isfinite(args.maximum_step) or args.maximum_step <= 0): p.error('maximum step must be finite positive')
    parent = json.loads(args.parent.read_text())
    result = build(parent, args.loss_db, args.maximum_step)
    result['resource_parent_plan_sha256'] = helpers.sha(args.parent)
    result['coarse_generator_sha256'] = helpers.sha(__file__)
    result['source_file_sha256'][SCRIPT] = helpers.sha(args.root/SCRIPT)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+'\n')
    print(json.dumps(dict(plan=str(args.output), sha256=helpers.sha(args.output), cells=result['cells'])))
    if args.probe_binary:
        value = next(j for j in result['jobs'] if j['stage'] == 'configure')
        argv = [s.replace('{BINARY}', str(args.probe_binary.resolve())).replace('{OUT}', str(args.output.parent.resolve()/'configure-only-unused')) for s in value['argv']]
        binary_before = helpers.sha(args.probe_binary)
        native = subprocess.run(argv, cwd=args.root, capture_output=True, text=True, timeout=10, check=False)
        data = json.loads(native.stdout) if native.returncode == 0 else None
        receipt = dict(plan_sha256=helpers.sha(args.output), job_id=value['id'], argv=argv,
            binary_sha256=binary_before, binary_unchanged=helpers.sha(args.probe_binary) == binary_before,
            exit_code=native.returncode, stdout=native.stdout, stderr=native.stderr,
            initial_timing_prior_covered=data['configuration']['public_search']['covers_initial_timing_prior'] if data else None,
            native_fine_rate_hz=data['configuration']['capture']['fine_rate_per_carrier_hz'] if data else None,
            scope='Local configure-only preflight; remote frozen binary still requires its own configure stage')
        args.probe_output.parent.mkdir(parents=True, exist_ok=True)
        args.probe_output.write_text(json.dumps(receipt, indent=2, sort_keys=True)+'\n')
        if native.returncode != 0 or not receipt['binary_unchanged'] or not receipt['initial_timing_prior_covered']:
            raise RuntimeError('native configure probe failed; retained receipt')

if __name__ == '__main__': main()
