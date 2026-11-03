#!/usr/bin/env python3
"""Verify retained global timing-bank scores and compact their paired baselines."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.special import logsumexp


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()


def outcome(scores,truth,control,gate):
    b=int(np.argmax(scores)); accepted=bool(scores[b]>=gate)
    if control=='signal':return dict(correct_admitted=int(accepted and b==truth),wrong_admitted=int(accepted and b!=truth),missed=int(not accepted))
    return dict(false_admitted=int(accepted),not_admitted=int(not accepted))


def summarize(remote,name):
    base=remote/'fixed'/name
    plan=json.loads((base/'plan.json').read_text());result=json.loads((base/'results.json').read_text())
    if result['plan_sha256']!=sha(base/'plan.json') or result['integrity_errors'] or not result['all_selected_jobs_accounted']:
        raise ValueError('global receipt integrity failure')
    def local(path):
        for prefix,sub in (('/workspace/triad-fixed/local-evidence/triad-broad-coverage/','fixed'),('/workspace/triad/local-evidence/triad-broad-coverage/','original')):
            if path.startswith(prefix):return remote/sub/path[len(prefix):]
        raise ValueError('unknown relocation')
    baseline={}
    for binding in plan['baseline_receipts']:
        p=local(binding['path'])
        if sha(p)!=binding['sha256']:raise ValueError('baseline receipt changed')
        r=json.loads(p.read_text())
        for j in r['jobs']:baseline[(r['plan_sha256'],j['id'])]=j
    definitions={j['id']:j for j in plan['jobs']};rows=[]
    for rec in result['jobs']:
        if rec['status']!='completed':continue
        job=definitions[rec['id']]
        for f in rec['output_files']:
            if (base/f['path']).stat().st_size!=f['size_bytes'] or sha(base/f['path'])!=f['sha256']:raise ValueError('changed scorer output')
        rp=next(base/f['path'] for f in rec['output_files'] if f['path'].endswith('.json'))
        ap=next(base/f['path'] for f in rec['output_files'] if f['path'].endswith('.npz'))
        report=json.loads(rp.read_text());capture=report['captures'][0]
        if report['scores_npz_sha256']!=sha(ap):raise ValueError('score archive differs')
        if report['receiver_source_sha256']!=plan['scorer_source_file_sha256']['tools/triad_group_acquire.py']:raise ValueError('receiver source differs')
        with np.load(ap,allow_pickle=False) as a:
            group=np.asarray(a['group_scores_0']);bits=np.asarray(a['bit_scores_0'])
        if not np.isfinite(group).all() or not np.array_equal(group,capture['group_bit_log_likelihoods']) or not np.array_equal(bits,capture['bit_log_likelihoods']):raise ValueError('raw score/JSON mismatch')
        if not np.allclose(logsumexp(group,axis=0)-np.log(len(group)),bits,rtol=0,atol=2e-12):raise ValueError('public timing mixture differs')
        evaluated=capture['simulation_evaluation_only'];truth=evaluated['signal_bit'];control=evaluated['control']
        if truth!=job['bit'] or control!=job['control']:raise ValueError('evaluation identity differs')
        old=baseline[(job['input_plan_sha256'],job['original_job_id'])]
        if old['status']!='completed' or old['acquisition_summary']!=rec['same_capture_short_window_baseline']:raise ValueError('paired baseline differs')
        for variant,summary in (('global',rec['acquisition_summary']),('short-window',old['acquisition_summary'])):
            for target in ('strict','relaxed'):
                gate=summary['gates'][target]
                counted=outcome(summary['primary_scores'],truth,control,gate)
                if counted!=summary['primary_full_timing_mixture'][target]:raise ValueError('independent outcome recount differs')
                rows.append(dict(job_id=rec['id'],case=job['case'],cn0_db_hz=job.get('cn0_db_hz',20),
                    control=control,bit=truth,variant=variant,target=target,groups=len(group),counts=counted,
                    scores=summary['primary_scores'],nominal_timing_counts=summary['same_workspace_nominal_group'][target],
                    timing=summary['timing']))
    return dict(schema='triad-broad-acquisition-summary-v1',summarizer_source_sha256=sha(Path(__file__)),
        plan_sha256=sha(base/'plan.json'),receipt_sha256=sha(base/'results.json'),counts=result['counts'],
        scorer_source=plan['scorer_source_file_sha256'],rows=rows,
        limitations=['Paired selected captures, not independent validation or a reliability estimate.',
            'All public timing groups retained. Frequency/clock/epoch dimensions are not silently added.',
            'Global model assumes constant two-ray coefficients after declared deterministic path phases; strong stochastic evolution can invalidate it.',
            'Short-window summaries are independently recounted from source-bound baseline receipt scores; global NPZ arrays are also verified here.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--remote',type=Path,required=True)
    p.add_argument('--name',default='global-acquisition');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(summarize(a.remote,a.name),indent=2,sort_keys=True,allow_nan=False)+'\n')
