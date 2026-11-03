#!/usr/bin/env python3
"""Freeze20 sampled phase captures and paired CPU/GPU scoring jobs.

The portable argument definitions can be executed by an existing bounded
runner. Actual binary/archive/build-proof binding is the executing owner's
responsibility; this plan does not manufacture a build attestation.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[5]
HERE=Path(__file__).resolve().parent

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build_jobs(cn0=-60.,seed_offset=0):
    if isinstance(cn0,bool) or not isinstance(cn0,(int,float)) or not math.isfinite(cn0) or not -80<=cn0<=20:
        raise ValueError('C/N0 must be finite within[-80,20]dB-Hz for this phase study')
    if isinstance(seed_offset,bool) or not isinstance(seed_offset,int) or not 0<=seed_offset<=1000000000:
        raise ValueError('seed offset must be an exact integer within[0,1000000000]')
    jobs=[];exports=[]
    for control,count,start in [('signal',16,0),('null',4,100)]:
        for trial in range(count):
            ident=f'flat-tcxo-{control}-{trial:02d}';index=start+trial
            workspace='{OUT}/workspaces/'+ident
            argv=['{BINARY}','--mode','export-stats','--seconds','73440000','--rate','.001','--added','1',
                '--sample-rate','1','--sample-rate-denominator','4','--rf-hz','500000','--nominal-if-hz','.0005',
                '--channel','none','--phase-diffusion','.001234134149488435','--basis','flat','--cn0',format(cn0,'.17g'),
                '--threads','1','--memory-mib','256','--export-max-samples','25000000','--export-max-mib','512',
                '--block','250','--signal-bit',str(trial%2),'--control',control,'--output',workspace,'--stats-noisy']
            for domain,offset in [('seed',1803000),('key-seed',2803000),('channel-seed',3803000),
                ('oscillator-seed',4803000),('noise-seed',5803000),('frontend-seed',6803000),('path-seed',7803000)]:
                argv+=['--'+domain,str(offset+index+seed_offset)]
            cfg=argv.copy();cfg[cfg.index('--mode')+1]='configure'
            jobs.append(dict(id='configure-'+ident,stage='configure',depends_on=[],argv=cfg,timeout_seconds=10))
            jobs.append(dict(id=ident,stage='export',depends_on=['configure-'+ident],argv=argv,
                timeout_seconds=900,workspace=workspace,control=control,signal_bit=trial%2,trial=trial))
            exports.append((ident,workspace))
    script='{ROOT}/'+str((HERE/'score_actual_batch.py').relative_to(ROOT))
    for ident,stage,device,precision,implementation,selected in [
        ('actual-all20-cpu-fp32','actual','cpu','fp32','reference',exports),
        ('benchmark-cpu-fp32','benchmark','cpu','fp32','reference',exports[:1]),
        ('benchmark-cpu-fp64','benchmark','cpu','fp64','reference',exports[:1]),
        ('benchmark-cuda-fp32','benchmark','cuda:0','fp32','graph',exports[:1]),
        ('benchmark-cuda-fp64','benchmark','cuda:0','fp64','graph',exports[:1]),
        ('benchmark-all20-cuda-fp32','benchmark','cuda:0','fp32','graph',exports)]:
        report='{OUT}/scores/'+ident+'.json'
        jobs.append(dict(id=ident,stage=stage,depends_on=[x[0] for x in selected],
            argv=['{PYTHON}',script,*[x[1] for x in selected],'--output',report,'--device',device,
                  '--precision',precision,'--implementation',implementation,'--phase-states','32','--memory-mib','512'],
            report=report,timeout_seconds=900))
    for job in jobs:job['definition_sha256']=digest(job)
    return jobs


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cn0',type=float,default=-60.)
    p.add_argument('--seed-offset',type=int,default=0)
    a=p.parse_args()
    if a.output.exists():raise ValueError('plan output already exists')
    sources=[HERE/n for n in ('prepare_actual.py','score_actual_batch.py','score_actual.py','diagnostic.py')]
    sources += [ROOT/n for n in ('tools/triad_phase_frequency.py','tools/triad_phase_optimized.py',
        'tools/triad_state_workspace.py','tools/triad_bounded_run.py','src/triad_modem_v3.cpp',
        'src/triad_simulation_stream.cpp','include/datapump/triad_modem_v3.hpp','tools/triad-long.cpp')]
    missing=[str(x.relative_to(ROOT)) for x in sources if not x.is_file()]
    if missing:raise ValueError('required source closure is incomplete: '+', '.join(missing))
    plan=dict(schema='triad-flat-tcxo-actual-plan-v1',
        source_file_sha256={str(x.relative_to(ROOT)):sha(x) for x in sources},
        jobs=build_jobs(a.cn0,a.seed_offset),cn0_db_hz=a.cn0,seed_offset=a.seed_offset,
        scope='flat knownsync unitgain TCXO-Wiener isolation; actual sampled noise;16signals4nulls',
        required_execution_order=['configure','export','actual','benchmark'],
        execution_limits={'export_jobs':12,'actual_jobs':1,'benchmark_jobs':1,'child_seconds':900},
        execution_binding_requirements=['Bind actual native executable SHA256, actual sourcearchive and successful real buildproof before execution.',
            'Verify all listed source hashes and native executable hash before and after every stage.',
            'Use fresh outputpaths and bounded childprocess groups; retain failures, timeouts, skippeddependencies and full attempted denominators.',
            'Hash all actual export files and score reports; bind scoreinputs to successful retained export bytes.',
            'Every C/N0 or seed variant requires a fresh plan and outputroot; job names are intentionally shared across separately bound variants.',
            'Record fullprocess external walltime; GPUbenchmarks serial with explicit synchronization.'],
        limitations=['Not a full GPSDO profile or unknownCFO/clock/epoch search.',
            'No two-ray or added-selective-modulation claim. Physical model extensions follow only after this baseline.',
            'Four singlecapture CPU/GPU jobs use identical firstcapture; small singlecapture may favorCPU. The additional20capture CUDAFP32batch reuses the actualCPUbatch observations and is throughput evidence, not new independent validation.',
            'All benchmarkjobs run sequentially, including the20capture CUDAFP32batch.',
            'Runner external elapsed_seconds is fullprocess; wrapper separately reports setup and synchronized update cost.',
            '16signals4nulls establish feasibility only, not99% or rare daily false-admission qualification.'])
    if len({j['id'] for j in plan['jobs']})!=len(plan['jobs']):raise ValueError('duplicate job identity')
    if any(not 0<j['timeout_seconds']<=900 for j in plan['jobs']):raise ValueError('child bound exceeds900 seconds')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(plan,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(jobs=len(plan['jobs']),source_files=len(sources),binary_binding='executor_required')))


if __name__=='__main__':main()
