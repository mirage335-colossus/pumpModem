#!/usr/bin/env python3
"""Freeze long resource recovery, throughput and independent sampled validation."""
from __future__ import annotations
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path

HERE=Path(__file__).resolve().parent
PREFIX='docs/validation-data/triad-v3/2026-11-03-broad/long_followup/'
STAGES=('configure','export','actual','resource','benchmark')

def load_file(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

fixed_prepare=load_file('long_fixed_prepare',HERE.parent/'rescore/prepare.py')
runner=fixed_prepare.runner;sha=runner.sha;digest=runner.digest

def atomic_json(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():raise FileExistsError('refusing to replace an existing receipt: '+str(path))
    temporary=path.with_name(path.name+'.pending')
    with temporary.open('x') as stream:
        json.dump(value,stream,indent=2,sort_keys=True,allow_nan=False);stream.write('\n');stream.flush();os.fsync(stream.fileno())
    os.replace(temporary,path)

def replace(argv,key,*values):
    if key in argv:
        first=argv.index(key);last=first+1
        while last<len(argv) and not argv[last].startswith('--'):last+=1
        argv[first:last]=[key,*map(str,values)]
    else:argv.extend([key,*map(str,values)])

def finish(job):
    job.pop('definition_sha256',None);job['definition_sha256']=digest(job);return job

def seed_values(index):
    return {flag:base+index for flag,base in [('--seed',1100000),('--key-seed',2100000),
        ('--channel-seed',3100000),('--oscillator-seed',4100000),('--noise-seed',5100000),
        ('--frontend-seed',6100000),('--path-seed',7100000)]}

def build_jobs(core,fixed):
    jobs=[];old={j['id']:j for j in core['jobs']};corrected={j['id']:j for j in fixed['jobs']}
    configs=core['configuration_cases']
    if len(configs)!=4:raise ValueError('expected exactly four long physical configurations')
    # Resource recovery is an explicit cap-only variant of the repaired
    # Gaussian jobs; no success here may be attributed to a new algorithm.
    for original in fixed['jobs']:
        if original['method']!='gaussian-window-ucb':continue
        job=copy.deepcopy(original);ident='resource2g-'+original['id']
        job.update(id=ident,stage='resource',variant='memory-cap-2gib',
            fixed_parent_id=original['id'],fixed_parent_definition_sha256=original['definition_sha256'],
            report='{OUT}/scores/'+ident+'.json',timeout_seconds=600)
        replace(job['argv'],'--memory-gib','2');replace(job['argv'],'--output',job['report'])
        jobs.append(finish(job))
    selected='long-rate-0.001-sub9-worst-b0-signal'
    reference=corrected['actual-'+selected+'-static-fft-ucb']
    for backend in ('cpu','cuda:0'):
        for precision in ('fp32','fp64'):
            ident='throughput-'+backend.replace(':','')+'-'+precision
            job=copy.deepcopy(reference)
            job.update(id=ident,stage='benchmark',variant='conditional-throughput32',
                backend=backend,precision=precision,noise_trials=32,cn0_grid=[-66,-63,-60,-57],
                fixed_parent_id=reference['id'],fixed_parent_definition_sha256=reference['definition_sha256'],
                control='conditional-signal-and-null',report='{OUT}/scores/'+ident+'.json',timeout_seconds=600)
            for key,value in [('--mode','conditional'),('--device',backend),('--precision',precision),
                ('--memory-gib','8'),('--noise-trials','32'),('--trial-batch','32'),('--seed','9200000'),('--output',job['report'])]:
                replace(job['argv'],key,value)
            job['argv']+=['--cn0-grid=-66,-63,-60,-57','--controls','signal','null']
            jobs.append(finish(job))
    pilot=[]
    for case_index,row in enumerate(configs):
        name=row['case']['name'];template=old[name+'-b0-signal']
        receiver=corrected['actual-'+name+'-b0-signal-static-fft-ucb']
        trials=[(trial,'signal',trial%2) for trial in range(32)]+[(40,'null',case_index%2),(41,'wrong-key',case_index%2)]
        for trial,control,bit in trials:
            ident=f'validation-{name}-t{trial:02d}-{control}'
            index=30000+100*case_index+trial
            export=copy.deepcopy(template)
            export.update(id=ident,stage='export',variant='independent-long-path',bit=bit,control=control,
                seed_address_index=index,trial=trial,workspace='{OUT}/workspaces/'+ident,
                parent_export_id=template['id'],parent_export_definition_sha256=template['definition_sha256'],
                depends_on=['configure-'+ident],timeout_seconds=600,pairing=None)
            for key,value in [('--signal-bit',bit),('--control',control),('--output',export['workspace']),
                ('--threads',1),('--export-max-samples',128000000),('--export-max-mib',2048),*seed_values(index).items()]:
                replace(export['argv'],key,value)
            cfg=copy.deepcopy(export);cfg.update(id='configure-'+ident,stage='configure',depends_on=[],timeout_seconds=10)
            cfg.pop('workspace');replace(cfg['argv'],'--mode','configure')
            jobs.extend([finish(cfg),finish(export)])
            score=copy.deepcopy(receiver);sid='actual-'+ident+'-static-fft-ucb'
            score.update(id=sid,stage='actual',variant='independent-long-path',bit=bit,control=control,trial=trial,
                seed_address_index=index,workspace=export['workspace'],report='{OUT}/scores/'+sid+'.json',
                depends_on=[ident],parent_export_id=template['id'],timeout_seconds=600)
            score['argv']=[export['workspace'] if x==receiver['workspace'] else x for x in score['argv']]
            replace(score['argv'],'--memory-gib','2');replace(score['argv'],'--threads',1);replace(score['argv'],'--output',score['report'])
            jobs.append(finish(score))
            if trial==0:pilot.append(ident)
    if len({j['id'] for j in jobs})!=len(jobs):raise ValueError('duplicate followup job')
    return jobs,pilot

def read_fixed(manifest):
    ref=manifest['fixed_manifest'];path=Path(ref['path'])
    if sha(path)!=ref['sha256']:raise ValueError('fixed manifest changed')
    fixed=json.loads(path.read_text());core,baseline=fixed_prepare.verify_manifest(fixed)
    if core.get('tier')!='long':raise ValueError('resource followup requires accepted long rescore manifest')
    definitions={j['id']:j for j in fixed['jobs']};records={}
    for ref in manifest['fixed_receipts']:
        path=Path(ref['path'])
        if sha(path)!=ref['sha256']:raise ValueError('fixed receipt changed')
        value=json.loads(path.read_text())
        if value.get('manifest_sha256')!=manifest['fixed_manifest']['sha256'] or value.get('variant')!='numerical-fix':
            raise ValueError('fixed receipt identity differs')
        if not value.get('inputs_unchanged_at_end') or not value.get('all_selected_jobs_accounted'):
            raise ValueError('fixed receipt inputs changed or accounting incomplete')
        if value['source_file_sha256']!=fixed['source_file_sha256'] or value['binary_sha256']!=fixed['binary']['sha256']:
            raise ValueError('fixed receipt source/binary binding differs')
        for record in value['jobs']:
            ident=record['id']
            if ident in records or record['definition_sha256']!=definitions[ident]['definition_sha256']:
                raise ValueError('duplicate or mismatched fixed job')
            for item in record.get('output_files',[]):
                file=Path(fixed['output_root'])/item['path']
                if file.stat().st_size!=item['size_bytes'] or sha(file)!=item['sha256']:raise ValueError('fixed output changed')
            records[ident]=record
    if set(records)!=set(definitions):raise ValueError('all16 fixed long results must be present')
    for ident,job in definitions.items():
        if job['method']!='gaussian-window-ucb':continue
        record=records[ident]
        if record['status']=='completed':raise ValueError('resource arm requires prior memory-preflight failure: '+ident)
        report=Path(job['report'].replace('{OUT}',fixed['output_root']))
        data=json.loads(report.read_text())
        paths=data.get('paths',[])
        if len(paths)!=1 or paths[0].get('error_type')!='MemoryError':
            raise ValueError('Gaussian prior failure is not the declared memory preflight: '+ident)
    return core,fixed,baseline,records

def verify_manifest(manifest):
    if digest({k:v for k,v in manifest.items() if k!='definition_sha256'})!=manifest['definition_sha256']:
        raise ValueError('followup manifest definition differs')
    core,fixed,baseline,records=read_fixed(manifest)
    fixed_prepare.verify_hashes(Path(fixed['patched_root']),manifest['source_file_sha256'])
    jobs,pilot=build_jobs(core,fixed)
    if jobs!=manifest['jobs'] or pilot!=manifest['pilot_export_ids']:raise ValueError('followup scientific job mapping differs')
    return core,fixed,baseline,records

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixed-manifest',type=Path,required=True);p.add_argument('--fixed-receipts',nargs='+',type=Path,required=True)
    p.add_argument('--output-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(argv)
    if args.output.exists() or args.output_root.exists():p.error('manifest and output root must be fresh')
    result=dict(schema='triad-broad-long-followup-v1',
        fixed_manifest=dict(path=str(args.fixed_manifest.resolve()),sha256=sha(args.fixed_manifest)),
        fixed_receipts=[dict(path=str(path.resolve()),sha256=sha(path)) for path in args.fixed_receipts],
        output_root=str(args.output_root.resolve()))
    core,fixed,baseline,records=read_fixed(result)
    if args.output_root.resolve().is_relative_to(Path(fixed['original_root'])):p.error('outputs cannot modify original source tree')
    root=Path(fixed['patched_root'])
    result['source_file_sha256']={**fixed['source_file_sha256'],**{PREFIX+name:sha(root/PREFIX/name) for name in ('prepare.py','run.py','test_plan.py')}}
    result['jobs'],result['pilot_export_ids']=build_jobs(core,fixed)
    result['stage_counts']={s:sum(j['stage']==s for j in result['jobs']) for s in STAGES}
    result['fixed_parent_status']={ident:record['status'] for ident,record in records.items()}
    result['targets']=core['targets']
    result['limits']=dict(configure_workers=20,export_workers=20,actual_workers=8,resource_workers=8,benchmark_workers=1,
        maximum_child_seconds=600,maximum_stage_seconds=7200,validation_paths_per_model=32,actual_null_controls=4,actual_wrong_key_controls=4)
    result['limitations']=['Memory-cap recovery changes resources only, not algorithm; report prior preflight failure separately.',
        'Four conditional throughput jobs share one physical path and noise address; not independent channel validation.',
        '128 fresh signal paths are32/model; even32/32 cannot establish99% reception. Four null/four wrong-key controls do not qualify rare admissions.',
        'Preserves850-day stable-ray model and all native physical settings; no claim about arbitrary diffuse fading or real RF retuning.',
        'Completed/hash-verified receipts commit results; partial native or receiver files from failed jobs remain failed artifacts, never dependency inputs.']
    result['definition_sha256']=digest(result);atomic_json(args.output,result)
    print(json.dumps({'output':str(args.output),'sha256':sha(args.output),'stage_counts':result['stage_counts'],'pilot_export_ids':result['pilot_export_ids']}))

if __name__=='__main__':main()
