#!/usr/bin/env python3
"""Freeze a small public timing-bank recovery experiment; never execute it."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
PREFIX = 'docs/validation-data/triad-v3/2026-11-03-broad/'
CASES = ('rate-360', 'spread-hf-worst', 'timing-margin-default-1',
         'erasures-none', 'erasures-cycle-edge')
NATIVE = ('tools/triad-long.cpp', 'include/datapump/triad_modem_v3.hpp',
          'src/triad_modem_v3.cpp', 'src/triad_simulation_stream.cpp',
          'src/triad_receiver_v3.cpp', 'src/channel_perturbation.hpp',
          'src/channel_spread.hpp', 'src/triad_gaussian_fading.hpp',
          'src/tuning.cpp', 'src/tuning_presets.cpp', 'tools/triad_bounded_run.py')
SCORER = ('tools/triad_group_acquire.py', 'tools/triad_gaussian_window.py',
          'tools/triad_noise_likelihood.py', 'tools/triad_state_workspace.py')

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda: stream.read(1024*1024), b''): h.update(data)
    return h.hexdigest()

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()

def replace(argv, key, *values):
    if key in argv:
        first = argv.index(key); last = first + 1
        while last < len(argv) and not argv[last].startswith('--'): last += 1
        argv[first:last] = [key, *map(str, values)]
    else: argv.extend([key, *map(str, values)])

def option(argv, key):
    return argv[argv.index(key)+1]

def symmetric_grid(half, maximum_step):
    # Even intervals retain the predeclared zero-arrival baseline exactly up
    # to ordinary binary floating arithmetic. Both public endpoints are covered.
    intervals = max(2, math.ceil(2*half/maximum_step))
    intervals += intervals % 2
    step = 2*half/intervals
    if intervals+1 > 2500 or step > maximum_step: raise ValueError('timing bank exceeds declared limits')
    return [-half, half, step], intervals+1

def job(identifier, stage, argv, timeout, **metadata):
    value = dict(id=identifier, stage=stage, argv=argv,
                 timeout_seconds=timeout, **metadata)
    value['definition_sha256'] = digest(value)
    return value

def build(core, analysis, root):
    cases = {c['case']['name']: c for c in core['configuration_cases']}
    exports = {j['id']: j for j in core['jobs'] if j['stage'] == 'export'}
    energy = {e['export_id']: e for e in analysis['physical_energy_evaluation']}
    jobs = []; cells = []
    for name in CASES:
        case = cases[name]
        maximum = case['configure_reference']['search_estimates']['timing_maximum_step_seconds']
        half = 7.035 if case['case']['timing'] == 'default' else .090500000001
        grid, groups = symmetric_grid(half, maximum)
        fhss = name.startswith('erasures-')
        block, windows = (128, 2) if fhss else (16, 16)
        dt = block/case['sample_rate']
        cfo = case['receiver_frequency_bound_hz']
        cells.append(dict(case=name, timing_grid=grid, groups=groups,
                          nominal_group_index=(groups-1)//2, block_samples=block,
                          window_blocks=windows, window_seconds=dt*windows,
                          public_cfo_bound_hz=cfo,
                          worst_common_cfo_cycles_per_window=cfo*dt*windows,
                          timing_step_recommendation_seconds=maximum,
                          timing_loss_budget_db=.5,
                          timing_loss_scope='random-chip RRC/three-tone approximation, not a finite-pattern certificate',
                          projection_change='FHSS block16->128; preserve physical62.5ms likelihood window' if fhss else 'original block/window unchanged'))
        for bit, control in ((0, 'signal'), (1, 'signal'), (0, 'null')):
            original = exports[f'{name}-b{bit}-signal']
            ident = f'{name}-b{bit}-{control}'
            argv = list(original['argv'])
            changes = {'--mode': ['export-group-stats'], '--start': grid,
                       '--block': [block], '--control': [control],
                       '--output': ['{OUT}/workspaces/'+ident],
                       '--memory-mib': [512], '--export-max-mib': [2048],
                       '--export-max-samples': [2000000]}
            for key, values in changes.items(): replace(argv, key, *values)
            cfg = 'configure-'+ident
            configure = list(argv); replace(configure, '--mode', 'configure')
            metadata = dict(case=name, control=control, bit=bit,
                parent_export_id=original['id'], parent_export_definition_sha256=original['definition_sha256'],
                original_argv=original['argv'], permitted_changed_options=sorted(changes),
                expected_groups=groups, nominal_group_index=(groups-1)//2,
                window_blocks=windows, sample_rate=case['sample_rate'],
                parent_signal_evaluation=energy.get(original['id']))
            jobs.append(job(cfg, 'configure', configure, 10, depends_on=[], **metadata))
            jobs.append(job(ident, 'export', argv, 180, depends_on=[cfg],
                            workspace='{OUT}/workspaces/'+ident, **metadata))
            score_id = 'acquire-'+ident
            report = '{OUT}/scores/'+score_id+'.json'
            scores = '{OUT}/scores/'+score_id+'.npz'
            score = ['{PYTHON}', '{ROOT}/tools/triad_group_acquire.py',
                '{OUT}/workspaces/'+ident, '--doppler-width-hz', '0',
                '--window-blocks', str(windows), '--group-tile', '8',
                '--projection-tile-blocks', '256', '--memory-mib', '512',
                '--device', 'cpu', '--precision', 'fp32', '--threads', '1',
                '--attempts-per-day', '1440', '--duration-alternatives', '8',
                '--selection-methods', '16', '--calibration-segments', '2',
                '--evaluate-simulation', '--output', report, '--scores-output', scores]
            jobs.append(job(score_id, 'acquire', score, 180, depends_on=[ident],
                workspace='{OUT}/workspaces/'+ident, acquisition_report=report,
                score_archive=scores, **metadata))
    # Deliberately bind native and orchestration code separately from numerical
    # scorer variants. An accepted numerical fix need not regenerate waveforms.
    sources = {name: core['source_file_sha256'][name] for name in NATIVE}
    for name in (PREFIX+'reproduction/run_plan.py',
                 *(PREFIX+'acquisition/'+s for s in ('prepare.py', 'run.py'))):
        sources[name] = sha(root/name)
    return dict(schema='triad-broad-acquisition-plan-v1', jobs=jobs, cells=cells,
        source_file_sha256=sources,
        **{key:core[key] for key in ('build_provenance_sha256', 'source_archive_sha256', 'expected_binary_sha256')},
        targets=core['targets'], limits=dict(maximum_groups=2500, maximum_export_rows=2000000,
            maximum_export_bytes=2048*1024**2, maximum_child_seconds=180),
        limitations=[
            'Diagnostic followup selected after broad-screen failures; not an independent reliability confirmation.',
            'Two original signal paths and one control-only paired null per cell; no independent null/path qualification.',
            'All fixed timing groups enter one normalized mixture, without truth-guided selection or pruning.',
            'Zero common CFO, clock-rate and keyed-epoch receiver hypotheses remain explicit. The small configured OCXO bound gives at most the recorded phase cycles per likelihood window.',
            'Independent Gaussian windows with B=0 are a blockwise model; no full physical likelihood or universal sensitivity claim.',
            'FHSS source envelope and receiver sample erasures only; physical RF retuning remains outside this study.',
            'Same-workspace zero-arrival diagnostic retains full-bank calibration and admission gates; it is not a separately optimized receiver.',
            'Physical source seeds/arguments are inherited exactly except listed observation/export controls. Verify capture calendars before claiming paired signal samples.'])

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--core-plan', type=Path)
    p.add_argument('--analysis', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--scorer-manifest-only', action='store_true')
    args = p.parse_args()
    if args.output.exists(): p.error('output must be fresh')
    root = args.root.resolve()
    if args.scorer_manifest_only:
        result = dict(schema='triad-broad-acquisition-scorer-v1',
                      source_file_sha256={name:sha(root/name) for name in SCORER},
                      scope='Explicit numerical source variant; physical exports remain separately bound')
    else:
        if args.core_plan is None or args.analysis is None: p.error('core plan and analysis required')
        core = json.loads(args.core_plan.read_text()); analysis = json.loads(args.analysis.read_text())
        if analysis['plan_sha256'] != sha(args.core_plan): raise ValueError('analysis and parent plan differ')
        result = build(core, analysis, root)
        result.update(parent_plan_sha256=sha(args.core_plan), selection_analysis_sha256=sha(args.analysis))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+'\n')
    print(json.dumps(dict(output=str(args.output), sha256=sha(args.output), jobs=len(result.get('jobs', [])))))

if __name__ == '__main__': main()
