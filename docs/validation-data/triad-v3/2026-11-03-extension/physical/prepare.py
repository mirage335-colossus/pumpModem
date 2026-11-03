#!/usr/bin/env python3
"""Prepare (never execute) the bounded physical extension experiment."""
from __future__ import annotations
import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
BROAD = HERE.parents[1] / '2026-11-03-broad'
SPEC = importlib.util.spec_from_file_location('extension_timing', BROAD/'acquisition/coarse_fhss.py')
timing = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(timing)
NATIVE = timing.helpers.NATIVE
# The noise-likelihood helper imports the Gaussian-state helper even when the
# selected receiver is the window model. Bind that numerical dependency too.
SCORER = (*timing.helpers.SCORER, 'tools/triad_gaussian_state.py')
SCRIPTS = ('docs/validation-data/triad-v3/2026-11-03-extension/physical/prepare.py',
           'docs/validation-data/triad-v3/2026-11-03-extension/physical/run.py',
           'docs/validation-data/triad-v3/2026-11-03-broad/acquisition/coarse_fhss.py',
           'docs/validation-data/triad-v3/2026-11-03-broad/acquisition/prepare.py',
           'docs/validation-data/triad-v3/2026-11-03-broad/reproduction/run_plan.py')
LOSSES = {
    'middle': [11, 12],
    'random10': [3,17,25,33,41,47,52,53,87,101,109,111],
    'random25': [1,6,14,20,23,24,27,33,36,43,49,50,52,57,61,65,67,72,74,78,79,86,89,91,94,95,99,108,111],
}
SEED_FLAGS = ('--seed','--key-seed','--channel-seed','--oscillator-seed',
              '--noise-seed','--frontend-seed','--path-seed')
sha = timing.helpers.sha
digest = timing.helpers.digest
replace = timing.helpers.replace
option = timing.helpers.option


def remove(argv, key):
    if key not in argv: return
    first = argv.index(key); last = first + 1
    while last < len(argv) and not argv[last].startswith('--'): last += 1
    del argv[first:last]


def grid(half, maximum_step):
    intervals = max(2, math.ceil(2 * half / maximum_step))
    intervals += intervals % 2
    return [-half, half, 2 * half / intervals], intervals + 1


def definitions():
    out = [dict(name='loss-'+name, edits={'--missed-dwells': [','.join(map(str, loss))]})
           for name, loss in LOSSES.items()]
    out += [dict(name='internal-wait3', edits={'--burst-wait-tiles': [3]}, timeout=900),
            dict(name='rate1200-added1', edits={'--rate': [1200], '--added': [1]}),
            dict(name='rate36-added100', edits={'--rate': [36], '--added': [100]})]
    # These four matched non-FHSS cases exercise newly explicit model support.
    # Separate actual controls and independent seeds remain necessary.
    common = {'--rate': [36], '--added': [1], '--seconds': [8],
              '--spread-preset': ['none'], '--tap-delay-ms': [0], '--tap-doppler-hz': [0],
              '--selective-perturbation': ['each-65'], '--selective-modulation-rate-hz': [.1]}
    out += [dict(name='none-am-off', edits={**common, '--selective-modulation-rate-hz': [0]}, remove=['--fhss']),
            dict(name='none-am65', edits=common, remove=['--fhss']),
            dict(name='zero-shift-vlf-am65', edits={**common, '--rf-hz': [0], '--nominal-if-hz': [8270]}, remove=['--fhss']),
            dict(name='shared-microwave-am65', edits={**common, '--rf-hz': [5.8e9],
                 '--shift-oscillator': ['baseband-clock']}, remove=['--fhss'])]
    # Append-only and opt-in: preserve all ten existing cell/seed indices.
    out.append(dict(name='max-spreading-rate0p1', opt_in=True, remove=['--fhss'], edits={
        '--rate': [.1], '--added': [1000], '--sample-rate': [128], '--seconds': [8],
        '--block': [128], '--rf-hz': [30_000_000], '--nominal-if-hz': [1725],
        '--base-oscillator': ['gpsdo-ocxo'], '--shift-oscillator': ['gpsdo-ocxo'],
        '--margin': [1], '--spread-preset': ['hf-best'], '--selective-perturbation': ['each-65'],
        '--basis': ['two-ray'], '--tap-delay-ms': [.5], '--tap-doppler-hz': [.1]}))
    return out


def build(parent, suite='paired', selected=None, cn0=20., seconds=None):
    if not math.isfinite(cn0) or (seconds is not None and (not math.isfinite(seconds) or seconds<8)):
        raise ValueError('finite C/N0 and requested duration at least8seconds required')
    templates = {j['bit']: j for j in parent['jobs']
                 if j['stage']=='export' and j['case']=='erasures-none' and j['control']=='signal'}
    if set(templates) != {0,1}: raise ValueError('parent must retain both no-loss signal argv definitions')
    cells=[]; jobs=[]
    specs = definitions()
    if selected:
        if not set(selected) <= {c['name'] for c in specs}: raise ValueError('unknown cell')
        specs = [c for c in specs if c['name'] in selected]
    else:
        specs = [c for c in specs if not c.get('opt_in',False)]
    for cell_index, spec in enumerate(definitions()):
        if spec not in specs: continue
        base = list(templates[0]['argv'])
        for key in spec.get('remove', []): remove(base, key)
        for key, values in spec['edits'].items(): replace(base, key, *values)
        replace(base,'--cn0',cn0)
        if seconds is not None: replace(base,'--seconds',seconds)
        rate=float(option(base,'--rate')); added=int(option(base,'--added'))
        fine=rate*added/6; spacing=1.25*fine
        maximum=timing.maximum_step_for_loss(3., fine, spacing)
        bank, groups=grid(.090500000001, maximum)
        cell=dict(name=spec['name'], rate_hz=rate, added_symbols=added,
                  timing_grid=bank, group_count=groups, block_samples=128,
                  timing_loss=timing.loss_description(bank[2], fine, spacing),
                  timing_loss_budget_db=3., export_timeout_seconds=spec.get('timeout',600),
                  physical_rf_retuning=False, suite=suite, cn0_db_hz=cn0,
                  requested_seconds=float(option(base,'--seconds')),
                  within_bit_processing_waits=('--burst-wait-tiles' in spec['edits']))
        cells.append(cell)
        trials = [(0,'signal',0),(1,'signal',1),(0,'null',0)] if suite=='paired' else [
            *((i%2,'signal',i) for i in range(16)), (0,'null',40), (1,'wrong-key',41)]
        for bit, control, index in trials:
            ident=f"{spec['name']}-{suite}-{index:02d}-b{bit}-{control}"
            argv=list(templates[bit]['argv'])
            for key in spec.get('remove', []): remove(argv,key)
            for key, values in spec['edits'].items(): replace(argv,key,*values)
            replace(argv,'--cn0',cn0)
            if seconds is not None: replace(argv,'--seconds',seconds)
            for key, values in {'--start':bank, '--control':[control], '--signal-bit':[bit],
                                '--output':['{OUT}/workspaces/'+ident]}.items(): replace(argv,key,*values)
            if suite=='validation':
                for domain, flag in enumerate(SEED_FLAGS, 1):
                    replace(argv, flag, domain*1_000_000 + 50_000 + cell_index*100 + index)
            metadata=dict(case=spec['name'], control=control, bit=bit, trial_index=index,
                          expected_groups=groups, suite=suite, workspace='{OUT}/workspaces/'+ident,
                          parent_signal_evaluation=templates[bit].get('parent_signal_evaluation') if suite=='paired' else None,
                          physical_pairing_scope='same seeds; changed schedule/rate/model may change calendar and samples')
            cfg=list(argv); replace(cfg,'--mode','configure')
            jobs.append(timing.helpers.job('configure-'+ident,'configure',cfg,10,depends_on=[],**metadata))
            jobs.append(timing.helpers.job(ident,'export',argv,spec.get('timeout',600),
                                           depends_on=['configure-'+ident],**metadata))
            for method in ('window','global','global-frequency'):
                score_id=method+'-'+ident
                jobs.append(timing.helpers.job(score_id,'acquire',[],300, depends_on=[ident,'configure-'+ident],
                    method=method, acquisition_report='{OUT}/scores/'+score_id+'.json',
                    score_archive='{OUT}/scores/'+score_id+'.npz',**metadata))
    return dict(schema='triad-extension-physical-plan-v1', suite=suite, cells=cells, jobs=jobs,
        targets=dict(strict_correct_probability=.99, strict_false_admission_per_day=.0001,
                     relaxed_correct_probability=.9, relaxed_false_admission_per_12h=.01,
                     attempts_per_day=1440,duration_alternatives=8,selection_methods=16),
        limits=dict(maximum_native_groups=10000, maximum_receiver_base_groups=5000,
                    maximum_export_rows=2000000,
                    maximum_export_bytes=2048*1024**2, maximum_child_seconds=900),
        declared_unsupported=[dict(configuration='default 14.07s timing range, rate360 Added10',
            reason='~35203 timing groups at this 3dB approximation exceeds native10000 and receiver5000 base-group caps; no implicit coarse replacement'),
            dict(configuration='1MHz/30MHz automatic geometry',reason='prior native geometry rejects; not sampled here')],
        limitations=['Three controls per paired cell are diagnostics, not reliability qualification.',
            'Validation16 signals per cell is a separate independent seeded screen, not a99% qualification.',
            'Timing3dB is infinite-RRC/random-chip approximation, not a finite-pattern certificate.',
            'RF hopping is envelope plus receiver sample erasure only; no physical retuning.',
            'Public CFO grid uses conservative nominal bounds and full observation calendar; constant-clock and epoch hypotheses remain zero.',
            'Window/global/global-frequency alternatives share a finite predeclared family, not independent observations.',
            'No truth alignment, energy or realized oscillator value enters receiver selection.'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent',type=Path,required=True); p.add_argument('--root',type=Path,required=True)
    p.add_argument('--binary',type=Path); p.add_argument('--output',type=Path,required=True)
    p.add_argument('--suite',choices=('paired','validation'),default='paired')
    p.add_argument('--cn0',type=float,default=20.)
    p.add_argument('--seconds',type=float,help='explicit duration sweep; omitted preserves each baseline request')
    p.add_argument('--cell',action='append'); p.add_argument('--freeze',action='store_true')
    a=p.parse_args()
    if a.output.exists(): p.error('output must be fresh')
    parent=json.loads(a.parent.read_text())
    plan=build(parent,a.suite,a.cell,a.cn0,a.seconds)
    plan['parent_input_sha256']=sha(a.parent)
    plan['origin_plan_sha256']=parent.get('origin_plan_sha256',sha(a.parent))
    plan['frozen']=a.freeze
    if a.freeze:
        if a.binary is None: p.error('--freeze requires --binary')
        plan['expected_binary_sha256']=sha(a.binary)
        plan['source_file_sha256']={name:sha(a.root/name) for name in (*NATIVE,*SCRIPTS)}
        plan['scorer_source_file_sha256']={name:sha(a.root/name) for name in SCORER}
        plan['source_binary_relation']='Frozen source and executable identities; this plan is not a compilation attestation.'
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(plan,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(dict(output=str(a.output),sha256=sha(a.output),jobs=len(plan['jobs']),frozen=a.freeze)))


if __name__=='__main__': main()
