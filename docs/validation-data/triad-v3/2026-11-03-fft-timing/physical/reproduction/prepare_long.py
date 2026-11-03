#!/usr/bin/env python3
"""Optional850-day strong-modulation preflight; no exports or receiver runs."""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('fft_campaign_base',HERE/'prepare.py')
base=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(base)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','binary','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();root=a.root.resolve();binary=a.binary.resolve()
    if a.output.exists():p.error('output must be fresh')
    a.output.mkdir(parents=True)
    before={name:base.sha(root/name) for name in base.SOURCES};binary_hash=base.sha(binary)
    start=time.monotonic();deadline=start+60;rows=[];receipts=[]
    for depth in (25,65):
        c={**base.cells()[19],'name':'long850-each'+str(depth),'seconds':73_440_000,
           'selective':'each-'+str(depth),'cn0':-60,'preflight_only':False}
        argv=base.argv_for(c);probe=[x.replace('{BINARY}',str(binary)).replace('{OUTPUT}',str(a.output/'unused')) for x in argv]
        probe[probe.index('--mode')+1]='configure'
        remaining=deadline-time.monotonic()
        if remaining<=0:rows.append(dict(case=c,status='configure_not_started_deadline',trials=[]));continue
        try:result=subprocess.run(probe,cwd=root,capture_output=True,text=True,timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            rows.append(dict(case=c,status='native_configure_timeout',trials=[]));continue
        receipts.append(dict(case=c['name'],argv=probe,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
        if result.returncode:
            rows.append(dict(case=c,status='native_configuration_rejected',configure_receipt_index=len(receipts)-1,trials=[]));continue
        cfg=json.loads(result.stdout)['configuration'];bounds=base.preflight(cfg,c)
        trials=base.freeze_trials(c,argv,bounds) if bounds['status']=='eligible' else []
        for trial in trials:
            for name,value in trial['seed_roots'].items():
                value+=100_000;trial['seed_roots'][name]=value
                index=trial['export_argv'].index('--'+name);trial['export_argv'][index+1]=str(value)
        rows.append(dict(case=c,status=bounds['status'],configuration=cfg,preflight=bounds,
                         configure_receipt_index=len(receipts)-1,trials=trials))
    after={name:base.sha(root/name) for name in base.SOURCES}
    plan=dict(schema='triad-fft-physical-campaign-v1',root_approval_required_before_exports_or_scores=True,
        generator_sha256=base.sha(__file__),base_generator_sha256=base.sha(HERE/'prepare.py'),
        source_file_sha256=before,source_unchanged=before==after,native_binary_sha256=binary_hash,
        binary_unchanged=binary_hash==base.sha(binary),configure_elapsed_seconds=time.monotonic()-start,
        configure_budget_seconds=60,limits=dict(maximum_samples=base.MAX_SAMPLES,maximum_effective_groups=base.MAX_GROUPS,
            maximum_frequency_offsets=base.MAX_FREQUENCIES,memory_mib=1024,group_tile=512),
        controls_per_eligible_cell=dict(signal=4,null=2,wrong_key=2),
        root_seed_schedule_offset_from_base=100000,seed_schedule_scope='All seven domains offset100000 from the base eight newlydeclared roots; the same eight tuples are paired across Each25/Each65 only.',
        physical_scope='850day requested actual coordinate span at−60dB-Hz, rate.001 Added1, Fs1/128,RF0/IF.0005,OCXO/basebandclock,Sub9best.001Hz/.005ms margin1,corrected dispersiveEach25/65. Native Wiener phase and continuous channel state remain active; no phase-flat substitution.',
        limits_scope='This tests fixed-ray B=0 plus synthetic selective modulation and declared oscillator law, not arbitrary stochastic spread or calibrated850day field propagation. No inference to higher-Doppler−60dB-Hz qualification.',
        receiver_scope='Complete declared integer timing lattice and bounded CFO; fixed clock/epoch/drift,known physical tap hypotheses. Phase-continuity mismatch remains a receiver/model limitation.',
        post_export_guard='Use campaign/run.py actual rawsample,fulltemplate,memory,CFOcount and SHA256 guards before any score. Do not reinterpret requested850days as actual rounded calendar.',
        rows=rows)
    for name,value in (('plan.json',plan),('preflight.json',receipts)):
        (a.output/name).write_text(json.dumps(value,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(dict(plan=str(a.output/'plan.json'),sha256=base.sha(a.output/'plan.json'),
        statuses=[dict(case=r['case']['name'],status=r['status'],samples=r.get('preflight',{}).get('conservative_sample_upper_bound'),
            memory_mib=r.get('preflight',{}).get('cpu_explicit_array_bytes_upper',0)/2**20) for r in rows])))


if __name__=='__main__':main()
