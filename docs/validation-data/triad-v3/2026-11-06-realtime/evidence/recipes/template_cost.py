#!/usr/bin/env python3
"""Paired native generation with/without keyed template export; same waveform."""
import hashlib,json,pathlib,resource,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[2]; OUT=pathlib.Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
rows=[]
for enabled in (False,True):
    destination=OUT/('template-cost-'+str(int(enabled)))
    if destination.exists():raise RuntimeError('refusing to replace measurement')
    cmd=[str(ROOT/'build/dev-cli/triad_helper_export'),'--case','hf','--base-oscillator','crystal','--shift-oscillator','ic-7100','--channel-evolution','stochastic','--random-doppler-width-hz','0','--sample-rate','6000','--seed','20261105','--key-seed','2417','--output',str(destination)]
    if enabled:cmd+=['--templates']
    before=resource.getrusage(resource.RUSAGE_CHILDREN);w=time.perf_counter()
    subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True,timeout=60)
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    m=json.loads((destination/'manifest.json').read_text())
    rows.append(dict(templates=enabled,command=cmd,wall_seconds=time.perf_counter()-w,
        cpu_seconds=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,
        native_cost=m['cost'],observation_sha256=sha(destination/'observation.c128'),
        template_sha256=sha(destination/'templates.c128') if enabled else None))
report=dict(schema='triad-keyed-template-preparation-cost-v1',rows=rows,
    identical_observation=rows[0]['observation_sha256']==rows[1]['observation_sha256'],
    incremental_template_seconds={k:rows[1][k]-rows[0][k] for k in ('cpu_seconds','wall_seconds')},
    scope='One paired run: incremental raw keyed template construction and export I/O; not pure in-memory synthesis, simulation excluded by subtraction; not a confidence interval',
    binary_sha256=sha(ROOT/'build/dev-cli/triad_helper_export'))
(OUT/'template-cost.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
