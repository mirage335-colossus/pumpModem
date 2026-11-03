#!/usr/bin/env python3
"""Freeze independent long-span validation/duration/spread contrasts; configure only."""
from __future__ import annotations
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import time

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('fft_followup_base',HERE/'prepare.py')
base=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(base)
DEFAULT_LIMITS=dict(maximum_samples=2_000_000,maximum_effective_groups=1_000_000,
                    maximum_frequency_offsets=4096,memory_mib=1024,group_tile=512)


def replace(argv,flag,value):argv[argv.index(flag)+1]=str(value)


def trials(c,argv,bounds,limits,signals,controls):
    template=base.freeze_trials(c,argv,bounds)[0];result=[]
    sequence=[(i,i%2,'signal') for i in range(signals)]
    sequence += [(100+i,i%2,'null') for i in range(controls)]
    sequence += [(200+i,i%2,'wrong-key') for i in range(controls)]
    for index,bit,control in sequence:
        row=copy.deepcopy(template);ident=c['name']+'-trial'+str(index)+'-'+control
        row.update(id=ident,bit=bit,control=control,seed_index=index)
        roots={name:400_000_000+d*10_000_000+200_000+index for d,name in enumerate(base.SEED_FLAGS)}
        row['seed_roots']=roots
        for flag,value in (('--signal-bit',bit),('--control',control),('--output','{OUTPUT}/workspaces/'+ident),
                           ('--export-max-samples',limits['maximum_samples']),('--memory-mib',limits['memory_mib'])):
            replace(row['export_argv'],flag,value)
        for name,value in roots.items():replace(row['export_argv'],'--'+name,value)
        row['score_argv'][2]='{OUTPUT}/workspaces/'+ident
        replace(row['score_argv'],'--output','{OUTPUT}/scores/'+ident+'.json')
        replace(row['score_argv'],'--memory-mib',limits['memory_mib'])
        result.append(row)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','binary','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();root=a.root.resolve();binary=a.binary.resolve()
    if a.output.exists():p.error('fresh output required')
    a.output.mkdir(parents=True)
    before={name:base.sha(root/name) for name in base.SOURCES};binary_hash=base.sha(binary)
    start=time.monotonic();deadline=start+60;rows=[];receipts=[]
    specs=(('A850-best',73_440_000,'sub9-best',128,32,4,DEFAULT_LIMITS),
           ('B425-best',36_720_000,'sub9-best',128,16,2,DEFAULT_LIMITS),
           ('C850-moderate',73_440_000,'sub9-moderate',32,16,2,{**DEFAULT_LIMITS,'maximum_samples':4_000_000,'memory_mib':4096}))
    for name,seconds,spread,denominator,signals,controls,limits in specs:
        c={**base.cells()[19],'name':name,'seconds':seconds,'spread':spread,'fs_denominator':denominator,
           'selective':'each-65','cn0':-60,'preflight_only':False}
        argv=base.argv_for(c)
        replace(argv,'--export-max-samples',limits['maximum_samples']);replace(argv,'--memory-mib',limits['memory_mib'])
        probe=[x.replace('{BINARY}',str(binary)).replace('{OUTPUT}',str(a.output/'unused')) for x in argv]
        replace(probe,'--mode','configure');remaining=deadline-time.monotonic()
        if remaining<=0:rows.append(dict(case=c,status='configure_not_started_deadline',trials=[],limits=limits));continue
        try:proc=subprocess.run(probe,cwd=root,capture_output=True,text=True,timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            rows.append(dict(case=c,status='native_configure_timeout',trials=[],limits=limits));continue
        receipts.append(dict(case=name,argv=probe,returncode=proc.returncode,stdout=proc.stdout,stderr=proc.stderr))
        if proc.returncode:
            rows.append(dict(case=c,status='native_configuration_rejected',trials=[],limits=limits,configure_receipt_index=len(receipts)-1));continue
        cfg=json.loads(proc.stdout)['configuration'];bounds=base.preflight(cfg,c)
        old=list(bounds['blocked_reasons'])
        reasons=[r for r in old if r not in ('sample_cap_exceeded','cpu_explicit_array_memory_cap_exceeded')]
        if bounds['conservative_sample_upper_bound']>limits['maximum_samples']:reasons.append('sample_cap_exceeded')
        if bounds['cpu_explicit_array_bytes_upper']>=limits['memory_mib']*1024**2:reasons.append('cpu_explicit_array_memory_cap_exceeded')
        bounds.update(baseline_2m_1g_blocked_reasons=old,blocked_reasons=reasons,
            status='resource_or_capture_unavailable' if reasons else 'eligible',
            cuda_memory_budget_feasible=bounds['cuda_explicit_array_bytes_upper']<limits['memory_mib']*1024**2)
        planned=trials(c,argv,bounds,limits,signals,controls) if not reasons else []
        rows.append(dict(case=c,status=bounds['status'],configuration=cfg,preflight=bounds,limits=limits,
            planned_controls=dict(signal=signals,null=controls,wrong_key=controls),
            configure_receipt_index=len(receipts)-1,trials=planned))
    after={name:base.sha(root/name) for name in base.SOURCES}
    plan=dict(schema='triad-fft-physical-campaign-v1',root_approval_required_before_exports_or_scores=True,
        source_file_sha256=before,source_unchanged=before==after,native_binary_sha256=binary_hash,binary_unchanged=binary_hash==base.sha(binary),
        generator_sha256=base.sha(__file__),base_generator_sha256=base.sha(HERE/'prepare.py'),
        accepted_runner_v1_sha256=base.sha(HERE/'run-v1.py'),runner_sha256=base.sha(HERE/'run.py'),
        configure_elapsed_seconds=time.monotonic()-start,configure_budget_seconds=60,limits=DEFAULT_LIMITS,
        selection_scope='Followup chosen after base and initial long observations; these fresh seeds were fixed before this followup decode. Initial pilot successes are not independent validation trials in these cells.',
        seed_scope='Every domain base400M+domain*10M+200000+index; signalindices0..31(A),0..15(B/C),null100..103/101,wrong-key200..203/201. Matched indices use identical roots across A/B/C, but changed physical calendar/rate/spread changes samples and RNG consumption. Distinct from earlier base/+100000 seeds.',
        receiver_scope='Existing finite calibrated whole-observation joint two-ray likelihood, fixed model-informed taps, complete declared integer lag/CFO grids, clock0ppm/epochoffset0/drift0. No generic fading/phase-tracking qualification.',
        physical_scope='Each65,rate.001Added1,RF0/IF.0005,OCXO/basebandclock,margin1,actual continuous native path and oscillator state. A/B Sub9best .001Hz/.005ms; C Sub9moderate .005Hz/.05ms. Stable ray B=0; delay/Doppler values are known receiver-model inputs.',
        resource_scope='A/B retain2mrows and1GiB. Only C explicitly allows4mrows and4GiB per child. Hard runner ceilings reject larger plan limits. Four workers can require up to16GiB explicit arrays plus runtimes.',
        calibration_scope='Unchanged1440attempts/day,8durations,16methods; strict99%/1e-4day andrelaxed90%/.01per12h targets. Separate fixed physical configurations do not establish universal probability bounds; each model/calibration assumption remains explicit.',
        rows=rows)
    for name,value in (('plan.json',plan),('preflight.json',receipts)):
        (a.output/name).write_text(json.dumps(value,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(dict(plan=str(a.output/'plan.json'),sha256=base.sha(a.output/'plan.json'),
        cells=[dict(case=r['case']['name'],status=r['status'],captures=len(r['trials']),
            samples=r.get('preflight',{}).get('conservative_sample_upper_bound'),
            memory_mib=r.get('preflight',{}).get('cpu_explicit_array_bytes_upper',0)/2**20) for r in rows])))


if __name__=='__main__':main()
