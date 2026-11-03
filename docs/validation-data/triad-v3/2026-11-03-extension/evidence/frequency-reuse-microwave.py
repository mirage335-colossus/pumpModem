#!/usr/bin/env python3
"""Direct native CFO projections versus original-block midpoint reuse.

Fixed public endpoints/zero and all seven original public timing choices.
Same three paired physical paths/noise seeds at block sizes 128 and 64.
This is an approximation diagnostic, not a complete CFO acquisition bank.
"""
import argparse, json, math, resource, shutil, subprocess, sys, time
from pathlib import Path
import numpy as np
import torch
PARSER=argparse.ArgumentParser(description=__doc__)
PARSER.add_argument('--root',type=Path,required=True,help='repository root')
PARSER.add_argument('--binary',type=Path,required=True,help='source-matched native triad_long executable')
PARSER.add_argument('--output',type=Path,required=True,help='fresh directory for bounded captures and comparison')
PARSER.add_argument('--configuration-input',type=Path,default=Path(__file__).with_suffix('.json'),
                    help='retained comparison with configuration_inputs, or original configure-stage receipt')
PARSER.add_argument('--dry-run',action='store_true',help='validate fixed inputs and print export commands without executing')
ARGS=PARSER.parse_args()
ROOT=ARGS.root.resolve()
HERE=ARGS.output.resolve()
sys.path.insert(0,str(ROOT/'tools'))
from triad_group_acquire import AcquisitionConfig, GroupedWorkspace, score_workspace, frequency_reuse_bound, sha256

def replace(argv,key,*values):
    if key in argv:
        i=argv.index(key)+1; j=i
        while j<len(argv) and not argv[j].startswith('--'):j+=1
        argv[i:j]=list(map(str,values))
    else:argv.extend([key,*map(str,values)])

class ZeroFrequencyView:
    def __init__(self,parent):
        self.parent=parent
        self.indices=[i for i,k in enumerate(parent.sync_keys) if k['frequency_hz']==0.]
        assert self.indices
        self.groups=len(self.indices)
        self.mapping=parent.mapping[self.indices]
        self.selected_indices=parent.selected_indices[self.indices]
        self.sync_keys=[parent.sync_keys[i] for i in self.indices]
    def __getattr__(self,name):return getattr(self.parent,name)
    def read_group_tile(self,first,stop,reblock,frequency_offset_hz=0.):
        parts=[self.parent.read_group_tile(i,i+1,reblock,frequency_offset_hz) for i in self.indices[first:stop]]
        return tuple(np.concatenate([p[k] for p in parts],axis=1) for k in (0,1))

def identity(ws):
    return {name:sha256(ws.directory/name) for name in ('manifest.json','group_gram.c128','group_observation.c128',
       'received_energy.f64','intervals.u64','valid_rows.u64','signal_energy.f64')}

def outcome(score,control,bit):
    if control=='signal':
        return dict(correct=int(score['accepted'] and score['bit']==bit),wrong=int(score['accepted'] and score['bit']!=bit),miss=int(not score['accepted']))
    return dict(false_admission=int(score['accepted']),not_admitted=int(not score['accepted']))

def export_command(job,block,binary):
    stem=job['id'].removeprefix('configure-')
    bound=float(job['public_frequency_bound_hz'])
    if not math.isfinite(bound) or bound<=0:raise ValueError('positive finite public CFO bound required')
    argv=list(job['argv']);argv[0]=str(binary)
    for key,values in {'--mode':['export-group-stats'],'--output':[HERE/f'{stem}-block{block}'], '--block':[block],
                       '--frequency':[-bound,bound,bound],'--memory-mib':[256],'--export-max-mib':[64]}.items():
        replace(argv,key,*values)
    return argv

def main():
    torch.set_num_threads(1)
    record=ARGS.configuration_input.resolve()
    parent=json.loads(record.read_text())
    if 'configuration_inputs' in parent:
        jobs=parent['configuration_inputs']
    else:
        jobs=[]
        for j in parent['jobs']:
            if j['case']=='shared-microwave-am65':
                jobs.append(dict(id=j['id'],case=j['case'],argv=j['argv'],control=j['control'],bit=j['bit'],
                    public_frequency_bound_hz=j['native_configuration']['configuration']['deterministic_oscillator_cfo_bound_hz']))
    jobs=sorted(jobs,key=lambda j:j['id'])
    if len(jobs)!=3 or {(j['control'],j['bit']) for j in jobs}!={('signal',0),('signal',1),('null',0)}:
        raise ValueError('expected the fixed two signal paths and paired null')
    if ARGS.dry_run:
        for job in jobs:
            for block in (128,64):
                print(json.dumps(dict(case=job['id'],block_samples=block,argv=export_command(job,block,ARGS.binary.resolve()))))
        return
    if HERE.exists():raise ValueError('fresh output directory required')
    HERE.mkdir(parents=True)
    binary=HERE/'triad_long'
    original=sha256(ARGS.binary)
    shutil.copy2(ARGS.binary,binary)
    if sha256(binary)!=original or sha256(ARGS.binary)!=original:
        raise RuntimeError('native binary changed during snapshot')
    source=sha256(ROOT/'tools/triad_group_acquire.py')
    report=dict(format='triad-cfo-microwave-direct-comparison-v1',configuration_input_sha256=sha256(record),configuration_inputs=jobs,
        binary_sha256=original,receiver_sha256=source,script_sha256=sha256(__file__),
        limits=['Paired two signal paths and one null only; no reliability or rare false admission qualification.',
                'CFO endpoints and zero are a deliberately coarse public diagnostic bank, not complete continuous acquisition.',
                'The phase-arc bound is a template/operator bound, not a nonlinear log-likelihood error certificate.',
                'The .02 dB budget is a separate declared research arm; the receiver default remains .01 dB.',
                'Signal energy is read only after receiver scoring for simulation audit.'],captures=[])
    for job in jobs:
        stem=job['id'].removeprefix('configure-')
        bound=job['public_frequency_bound_hz']
        by_size={}
        for block in (128,64):
            directory=HERE/f'{stem}-block{block}'
            argv=export_command(job,block,binary)
            start=time.perf_counter()
            result=subprocess.run(argv,capture_output=True,text=True,timeout=60)
            receipt=dict(argv=argv,exit_code=result.returncode,seconds=time.perf_counter()-start,
                         stdout=result.stdout,stderr=result.stderr)
            (HERE/f'{stem}-block{block}-export.json').write_text(json.dumps(receipt,indent=2)+'\n')
            if result.returncode:raise RuntimeError(result.stderr)
            ws=GroupedWorkspace(directory);view=ZeroFrequencyView(ws)
            offsets=(-bound,0.,bound)
            assert ws.groups==3*view.groups
            direct=score_workspace(ws,AcquisitionConfig(0.,window_blocks=1,reblock=ws.blocks,memory_limit_bytes=128*1024**2))
            reused=score_workspace(view,AcquisitionConfig(0.,window_blocks=1,reblock=ws.blocks,
                frequency_offsets_hz=offsets,maximum_block_phase_loss_db=.02,memory_limit_bytes=128*1024**2))
            lookup={(k['arrival_seconds'],k['frequency_hz']):i for i,k in enumerate(ws.sync_keys)}
            order=[lookup[k['arrival_seconds'],offset] for offset in offsets for k in view.sync_keys]
            ds=np.asarray(direct['group_bit_log_likelihoods'])[order]
            rs=np.asarray(reused['group_bit_log_likelihoods'])
            projection=[]
            energy=np.asarray(ws.array('received_energy.f64'))
            for offset in offsets:
                worst=0.;maxgram=0.
                for vi,k in enumerate(view.sync_keys):
                    gi=lookup[k['arrival_seconds'],offset]
                    gd,rd=ws.read_group_tile(gi,gi+1,1)
                    gr,rr=view.read_group_tile(vi,vi+1,1,offset)
                    scale=np.sqrt(np.maximum(0,gd[:,0].diagonal(axis1=-2,axis2=-1).real*energy[:,None]))
                    good=scale>0
                    if good.any():worst=max(worst,float(np.max(abs(rd[:,0]-rr[:,0])[good]/scale[good])))
                    maxgram=max(maxgram,float(np.max(abs(gd-gr))))
                projection.append(dict(offset_hz=offset,max_normalized_correlation_error=worst,max_absolute_gram_error=maxgram,
                    bound=frequency_reuse_bound((offset,),block/ws.sample_rate,.02)))
            control,bit=job['control'],job['bit']
            entry=dict(case=stem,control=control,bit=bit,original_block_samples=block,physical_sample_count=ws.sample_count,
                sample_rate_hz=ws.sample_rate,capture_seconds=ws.sample_count/ws.sample_rate,source_blocks=ws.blocks,
                public_timing_values=[k['arrival_seconds'] for k in view.sync_keys],frequency_offsets_hz=offsets,
                input_sha256=identity(ws),projection_comparison=projection,
                maximum_group_log_score_error=float(np.max(abs(ds-rs))),
                maximum_mixture_log_score_error=float(np.max(abs(np.asarray(direct['bit_log_likelihoods'])-reused['bit_log_likelihoods']))),
                direct_bit_scores=direct['bit_log_likelihoods'],reused_bit_scores=reused['bit_log_likelihoods'],
                direct={gate:direct[gate] for gate in ('strict','relaxed')},reused={gate:reused[gate] for gate in ('strict','relaxed')},
                outcome={method:{gate:outcome(score[gate],control,bit) for gate in ('strict','relaxed')}
                         for method,score in (('direct',direct),('reused',reused))},
                received_energy_samples=math.fsum(energy),
                signal_energy_samples_simulation_only=math.fsum(np.fromfile(directory/'signal_energy.f64',dtype='<f8')),
                export_seconds=receipt['seconds'])
            report['captures'].append(entry);by_size[block]=ws
            print(json.dumps(dict(finished=stem,block=block,max_group_log_error=entry['maximum_group_log_score_error'])),flush=True)
        coarse,fine=by_size[128],by_size[64]
        gc,rc=coarse.read_group_tile(0,coarse.groups,1)
        gf,rf=fine.read_group_tile(0,fine.groups,2)
        ic,vc,ec=coarse.calendar(1);iff,vf,ef=fine.calendar(2)
        assert np.array_equal(ic,iff) and np.array_equal(vc,vf)
        report.setdefault('block_partition_checks',[]).append(dict(case=stem,
            same_physical_calendar=True,max_gram_difference=float(np.max(abs(gc-gf))),
            max_correlation_difference=float(np.max(abs(rc-rf))),
            max_received_energy_difference=float(np.max(abs(ec-ef))),
            scope='Matching indexed noisy projected observations and energy under two block partitions; raw I/Q was not exported.'))
    assert sha256(ROOT/'tools/triad_group_acquire.py')==source and sha256(binary)==original
    report['maximum_python_rss_kib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    report['maximum_native_child_rss_kib']=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    (HERE/'comparison.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(report=str(HERE/'comparison.json'),captures=len(report['captures']),
        python_rss_kib=report['maximum_python_rss_kib'],native_rss_kib=report['maximum_native_child_rss_kib'])))
if __name__=='__main__':main()
