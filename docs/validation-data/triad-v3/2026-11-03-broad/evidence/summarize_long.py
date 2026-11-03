#!/usr/bin/env python3
"""Recount long followup scores from a relocated complete evidence tree.

Requires the original/fixed remote evidence trees beneath --remote. Does not
launch receivers. A missing raw workspace is an error, not a validated input.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('long_recount_reference', HERE.parent/'reproduction/analyze.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024*1024), b''): h.update(chunk)
    return h.hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def analyze(remote, require_actual):
    prefix = '/workspace/triad-fixed/local-evidence/triad-broad-coverage/'
    oldprefix = '/workspace/triad/local-evidence/triad-broad-coverage/'
    def relocated(name):
        if name.startswith(prefix): return remote/'fixed'/name[len(prefix):]
        if name.startswith(oldprefix): return remote/'original'/name[len(oldprefix):]
        raise ValueError('unrecognized evidence path '+name)
    mp = remote/'fixed/long-followup/manifest.json'
    manifest = json.loads(mp.read_text())
    helper_relative='docs/validation-data/triad-v3/2026-11-03-broad/reproduction/analyze.py'
    if sha(Path(reference.__file__)) != manifest['source_file_sha256'][helper_relative]:
        raise ValueError('imported recount helper differs from frozen source closure')
    definitions = {j['id']: j for j in manifest['jobs']}
    if len(definitions) != len(manifest['jobs']): raise ValueError('duplicate job')
    for j in definitions.values():
        if digest({k:v for k,v in j.items() if k != 'definition_sha256'}) != j['definition_sha256']:
            raise ValueError('changed job definition')
    out = relocated(manifest['output_root'])
    fixedpath = relocated(manifest['fixed_manifest']['path'])
    if sha(fixedpath) != manifest['fixed_manifest']['sha256']: raise ValueError('fixed parent changed')
    fixed = json.loads(fixedpath.read_text())
    checked = {}
    def verify(path, expected):
        key = str(path)
        if key not in checked: checked[key] = sha(path)
        if checked[key] != expected: raise ValueError('artifact changed '+key)
    records = {}; bindings = []; stages = []
    for sub in ('resource-run','benchmark-run','configure','pilot-run','remainder-run','actual-run'):
        path = out/sub/'results.json'
        if not path.exists():
            if sub == 'actual-run' and require_actual: raise ValueError('actual stage incomplete')
            continue
        receipt = json.loads(path.read_text())
        if receipt['manifest_sha256'] != sha(mp) or not all(receipt[k] for k in
                ('inputs_unchanged_at_end','all_selected_jobs_accounted')):
            raise ValueError('invalid stage binding')
        if receipt['source_file_sha256'] != manifest['source_file_sha256'] or receipt['binary_sha256'] != fixed['binary']['sha256']:
            raise ValueError('stage source/binary differs')
        if receipt['fixed_manifest_sha256'] != sha(fixedpath): raise ValueError('stage fixed parent differs')
        bindings.append(dict(path=str(path.relative_to(remote)), sha256=sha(path)))
        stages.append(dict(stage=receipt['stage'], directory=sub, counts=receipt['counts'],
                           elapsed_seconds=receipt['elapsed_seconds'], concurrent_jobs=receipt['concurrent_jobs']))
        for r in receipt['jobs']:
            ident = r['id']
            if ident in records or r['definition_sha256'] != definitions[ident]['definition_sha256']:
                raise ValueError('duplicate or changed recorded job')
            for f in r.get('output_files', []):
                p = out/f['path']
                if p.stat().st_size != f['size_bytes']: raise ValueError('artifact size changed')
                verify(p, f['sha256'])
            records[ident] = r
    if require_actual and any(j['stage']=='actual' and ident not in records
                              for ident,j in definitions.items()):
        raise ValueError('actual receipt does not account for every planned actual job')
    rows = []; arrays = {}; failures = []; energy = {}; paired_identity = {}
    for ident, job in definitions.items():
        if job['stage'] not in ('actual','resource','benchmark'): continue
        rec = records.get(ident)
        if rec is None or rec['status'] != 'completed':
            failures.append(dict(job_id=ident, stage=job['stage'], case=job['case'],
                control=job['control'], status=rec['status'] if rec else 'unexecuted'))
        reportpath = out/job['report'].removeprefix('{OUT}/')
        if not reportpath.exists():
            if rec and rec['status']=='completed': raise ValueError('completed scorer has no report')
            continue
        report = json.loads(reportpath.read_text())
        sources = fixed['source_file_sha256']
        if report['source_sha256'] != sources['tools/triad-conditional-sweep.py'] or any(
                sources.get('tools/'+name) != value for name, value in report['dependency_sha256'].items()):
            raise ValueError('scorer source differs')
        wp = relocated(job['workspace'].replace('{OUT}', manifest['output_root']))
        wm = json.loads((wp/'manifest.json').read_text())
        if wm['signal_bit'] != job['bit']: raise ValueError('truth identity differs')
        npz = reportpath.with_name(report['scores_file'])
        verify(npz, report['scores_sha256'])
        if len(report['paths']) != 1: raise ValueError('unexpected physical path count')
        physical = report['paths'][0]
        if relocated(physical['input']) != wp: raise ValueError('physical input differs')
        conditional = job['stage'] == 'benchmark'
        if conditional:
            cfg=report['configuration']; argv=job['argv']
            for flag,field in (('--device','device'),('--precision','precision'),('--seed','seed')):
                if str(cfg[field]) != argv[argv.index(flag)+1]: raise ValueError('benchmark arithmetic/seed differs')
        hashes = physical.get('sampled_input_hashes' if conditional else 'input_hashes', {})
        expected_names = {'manifest.json','gram.c128','intervals.u64','valid_rows.u64'} | (
            {'mean.c128','signal_energy.f64'} if conditional else {'observation.c128','received_energy.f64'})
        if physical['conditions'] and set(hashes) != expected_names: raise ValueError('incomplete input identity')
        for name, value in hashes.items(): verify(wp/name, value)
        strict = float(report['family']['fixed_window_log_threshold'])
        if not math.isclose(strict, math.log(1440*8*16*2/.00005), rel_tol=2e-14): raise ValueError('gate changed')
        if job['stage'] == 'actual':
            e = np.fromfile(wp/'signal_energy.f64', dtype='<f8')
            if not np.isfinite(e).all() or (e < 0).any() or len(e) != wm['block_count']: raise ValueError('invalid signal energy')
            energy[ident] = dict(sample_count=wm['sample_count'], sample_rate=wm['sample_rate'],
                sampled_seconds=wm['sample_count']/wm['sample_rate'],
                post_erasure_energy_seconds=float(e.sum(dtype=np.float64)/wm['sample_rate']),
                available_energy_over_n0=float(e.sum(dtype=np.float64)/wm['sample_rate']*1e-6))
        seen = set()
        with np.load(npz, allow_pickle=False) as archive:
            for condition in physical['conditions']:
                control, cn0 = condition['control'], condition['cn0_db_hz']
                key = (control, cn0)
                if key in seen: raise ValueError('duplicate condition')
                seen.add(key)
                if conditional:
                    if control not in ('signal','null') or cn0 not in job['cn0_grid']: raise ValueError('unexpected conditional cell')
                elif control != job['control'] or cn0 is not None: raise ValueError('unexpected actual cell')
                if set(condition['methods']) != {job['method']}: raise ValueError('unexpected method')
                detail = condition['methods'][job['method']]
                scores = np.array(archive[detail['score_array']])
                n = job['noise_trials'] if conditional else 1
                if len(scores) != n or detail['threshold'] != strict: raise ValueError('trial count or gate changed')
                if reference.recount(scores, job['bit'], strict, control) != detail['counts']: raise ValueError('count mismatch')
                arrays[(ident, control, cn0)] = scores
                if conditional:
                    paired_identity[(ident,control,cn0)]=dict(inputs=hashes,draws=condition['draws_simulation_only'])
                for target, threshold in (('strict',strict), ('relaxed',strict-math.log(200))):
                    rows.append(dict(job_id=ident, stage=job['stage'], case=job['case'],
                        control=control, bit=job['bit'], cn0_db_hz=cn0 if conditional else -60,
                        target=target, counts=reference.recount(scores,job['bit'],threshold,control),
                        score_min=float(scores.min()), score_max=float(scores.max()),
                        receiver_seconds=detail['receiver_seconds'], report_seconds=report['wall_seconds'],
                        bounded_seconds=rec.get('elapsed_seconds') if rec else None,
                        partial=not rec or rec['status']!='completed' or physical['status']!='completed'))
        expected = {(c,v) for c in ('signal','null') for v in job.get('cn0_grid',[])} if conditional else {(job['control'],None)}
        if rec and rec['status']=='completed' and (seen != expected or physical['status']!='completed'):
            raise ValueError('completed job has missing conditions')
    groups = defaultdict(Counter); partial_groups = set(); planned = defaultdict(list)
    for ident,job in definitions.items():
        if job['stage']=='actual': planned[(job['case'],job['control'],job['bit'])].append(ident)
    for row in rows:
        if row['stage']=='actual':
            key=(row['case'],row['control'],row['target'],row['bit'])
            groups[key].update(row['counts'])
            if row['partial']: partial_groups.add(key)
    validation=[]
    for (case, control, target, bit), counts in sorted(groups.items()):
        n=counts['trials']; success=counts['correct_accepted' if control=='signal' else 'false_admitted']
        ids=planned[(case,control,bit)]
        complete=(len(ids)==n and (case,control,target,bit) not in partial_groups and
                  all(records.get(ident,{}).get('status')=='completed' for ident in ids))
        validation.append(dict(case=case, control=control, target=target, bit=bit, counts=dict(counts),
            planned_trials=len(ids), complete_planned_stratum=complete,
            interval=reference.interval(success,n,'Pointwise within one complete predeclared bit stratum, assuming independent identically distributed paths; not simultaneous across cells.') if complete else None))
    combined=[]
    for case in sorted({r['case'] for r in validation}):
        for target in ('strict','relaxed'):
            c=Counter(); strata=[]
            for r in validation:
                if r['case']==case and r['target']==target and r['control']=='signal':
                    c.update(r['counts']); strata.append(r)
            if not c: continue
            n=c['trials']; k=c['correct_accepted']
            complete=(len(strata)==2 and {r['bit'] for r in strata}=={0,1} and
                      all(r['complete_planned_stratum'] for r in strata) and
                      strata[0]['planned_trials']==strata[1]['planned_trials'])
            combined.append(dict(case=case,target=target,counts=dict(c),
                complete_equal_bit_design=complete,
                planned_trials=sum(len(planned[(case,'signal',b)]) for b in (0,1)),
                mean_success_one_sided95_lower=max(0.,k/n-math.sqrt(math.log(20)/(2*n))) if complete else None,
                all_success_one_sided95_lower=.05**(1/n) if complete and k==n else None,
                scope='Fixed equal-bit stratification; independent paths. Hoeffding lower bound for nonidentical Bernoulli means. All-success bound uses product(p_i)<=mean(p_i)^n; neither is simultaneous across configurations.'))
    comparisons=[]
    for ident in ('throughput-cpu-fp32','throughput-cuda0-fp32','throughput-cuda0-fp64'):
        ref='throughput-cpu-fp64'
        expected={(c,v) for c in ('signal','null') for v in definitions[ref]['cn0_grid']}
        complete=(all(records.get(x,{}).get('status')=='completed' for x in (ref,ident)) and
                  all({(c,v) for i,c,v in arrays if i==x}==expected for x in (ref,ident)))
        if not complete:
            comparisons.append(dict(reference=ref,candidate=ident,status='incomplete_no_parity_claim'))
            continue
        changes=Counter(); maximum=0.
        for key,a in arrays.items():
            if key[0]!=ref or (ident,*key[1:]) not in arrays: continue
            b=arrays[(ident,*key[1:])]
            if paired_identity[key] != paired_identity[(ident,*key[1:])]: raise ValueError('benchmark physical inputs or noise draws differ')
            if a.shape != b.shape: raise ValueError('paired shape mismatch')
            maximum=max(maximum,float(np.max(np.abs(a-b))))
            changes['winner_changes']+=int(np.sum(a.argmax(axis=1)!=b.argmax(axis=1)))
            for name, gate in (('strict',22.72106338958574),('relaxed',17.422746023037703)):
                changes[name+'_admission_changes']+=int(np.sum((a.max(axis=1)>=gate)!=(b.max(axis=1)>=gate)))
        comparisons.append(dict(reference=ref,candidate=ident,status='complete_paired_cells',changes=dict(changes),
            maximum_absolute_score_change=maximum,
            reference_bounded_seconds=records[ref].get('elapsed_seconds'),candidate_bounded_seconds=records[ident].get('elapsed_seconds')))
    return dict(schema='triad-broad-long-summary-v1', manifest_sha256=sha(mp),
        summarizer_source_sha256=sha(Path(__file__)), recount_helper_sha256=sha(Path(reference.__file__)),
        fixed_manifest_sha256=sha(fixedpath), source_closure=manifest['source_file_sha256'],
        verified_file_count=len(checked), stages=stages, receipt_bindings=bindings, failures=failures,
        conditions=rows, validation_by_bit=validation, validation_equal_bit_mean=combined,
        energy_evaluation_only=energy, precision_comparisons=comparisons,
        limitations=['Conditional draws share one physical path and are not independent channel validation.',
            '850-day native calendars are genuine sampled simulation, not850days of laboratory observations.',
            'Four null/four wrong-key holdouts are diagnostic only; noise family calibration remains assumption-dependent.',
            'No frequency/clock/epoch timing grid outside each frozen public bank is implied.',
            'Failed and unexecuted jobs remain listed. Completed-cell counts are not a reliability denominator when failures are present.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--remote',type=Path,required=True)
    p.add_argument('--require-actual',action='store_true')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    a.output.write_text(json.dumps(analyze(a.remote,a.require_actual),indent=2,sort_keys=True,allow_nan=False)+'\n')
