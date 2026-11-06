#!/usr/bin/env python3
"""Bounded actual-receiver stage timings; never runs the full Cartesian bank."""
import os
os.environ.update(OMP_NUM_THREADS='2',OPENBLAS_NUM_THREADS='2',MKL_NUM_THREADS='2',NUMEXPR_NUM_THREADS='2')
import sys,time,json,math,subprocess,resource,hashlib,platform,gc
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools'))
def save(name,obj):
    (OUT/name).write_text(json.dumps(obj,indent=2,default=lambda x:x.tolist() if hasattr(x,'tolist') else str(x)))
def timing(fn):
    c=time.process_time();w=time.perf_counter();r=fn();return r,dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)
def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()
started=time.perf_counter(); report={'schema':'triad-cpu-ceiling-stage-experiment-v1','started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'scope':'Engineering sample-tile arithmetic forecast, not qualified full-bank runtime or calibrated admission','seed':20261105,'key_seed':2417}
cmd=[str(ROOT/'build/dev-cli/triad_helper_export'),'--case','hf','--base-oscillator','crystal','--shift-oscillator','ic-7100','--channel-evolution','stochastic','--random-doppler-width-hz','0','--sample-rate','6000','--templates','--seed','20261105','--key-seed','2417','--output',str(OUT/'capture')]
if not (OUT/'capture/manifest.json').exists():
    before=resource.getrusage(resource.RUSAGE_CHILDREN); w=time.perf_counter()
    r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    report['native_generation']={'command':cmd,'returncode':r.returncode,'wall_seconds':time.perf_counter()-w,'user_seconds':after.ru_utime-before.ru_utime,'system_seconds':after.ru_stime-before.ru_stime,'stdout':r.stdout,'stderr':r.stderr}
    save('generation.json',report['native_generation'])
    if r.returncode:raise RuntimeError(r.stderr)
else:report['native_generation']=json.loads((OUT/'generation.json').read_text())
print('CAPTURE_READY',flush=True)
_,report['numeric_import']=timing(lambda:exec('import numpy, scipy, torch'))
import numpy as np,scipy,torch
import ctypes,glob,contextlib
_blas=[]
for package,suffix in (('numpy','64_'),('scipy','')):
    path=glob.glob(str(Path(np.__file__).parent.parent/(package+'.libs')/'*openblas*'))[0]
    lib=ctypes.CDLL(path);setter=getattr(lib,'scipy_openblas_set_num_threads'+suffix);getter=getattr(lib,'scipy_openblas_get_num_threads'+suffix)
    setter.argtypes=[ctypes.c_int];getter.restype=ctypes.c_int;_blas.append((path,setter,getter))
def threadpool_info(): return [{'library':p,'threads':g()} for p,s,g in _blas]+[{'library':'torch','threads':torch.get_num_threads()}]
@contextlib.contextmanager
def threadpool_limits(limits):
    old=[g()for p,s,g in _blas]
    for p,s,g in _blas:s(limits)
    try:yield
    finally:
        for (p,s,g),n in zip(_blas,old):s(n)

from triad_dynamic_joint import load_public
from triad_dynamic_acquire import TiledAcquisition,AcquisitionTile
from triad_dynamic_frequency import estimate_dynamic_frequency
torch.set_num_threads(2);torch.set_num_interop_threads(1);torch.set_float32_matmul_precision('highest')
inputs,report['input_loading']=timing(lambda:load_public(OUT/'capture'))
m,templates,epochs,raw,valid=inputs
ch=m['channel'];cal=m['calendar'];fs=m['sample_rate_hz'];f=m['format']
bound=ch['deterministic_cfo_bound_hz']*ch['spread_margin'];clock=ch['sample_clock_pair_ppm']*ch['spread_margin']
a,b=cal['arrival_prior_seconds'];fine=f['fine_chip_rate_hz'];arrival_n=math.ceil((b-a)*4*fine)+1
arrivals=np.linspace(a,b,arrival_n);freqs=np.linspace(-bound,bound,365)
tmax=max(abs(-cal['capture_lead_seconds']),abs((len(raw)-1)/fs-cal['capture_lead_seconds']))
clock_intervals=math.ceil(2*clock*1e-6*tmax/(2/(8*fine)))
clocks=np.linspace(-clock,clock,clock_intervals+1)
candidates=len(epochs)*len(arrivals)*len(freqs)*len(clocks)
report['geometry']={'sample_rate_hz':fs,'raw_samples':len(raw),'capture_seconds':len(raw)/fs,'onair_seconds':cal['helper_end_seconds'],'data_seconds':f['bit_seconds'],'fine_rate_hz':fine,'cfo_bound_hz':bound,'frequency_count':len(freqs),'frequency_step_hz':freqs[1]-freqs[0],'arrival_count':len(arrivals),'arrival_step_seconds':arrivals[1]-arrivals[0],'clock_count':len(clocks),'clock_step_ppm':clocks[1]-clocks[0] if len(clocks)>1 else 0,'max_nominal_time_seconds':tmax,'clock_halfstep_endpoint_seconds':(clocks[1]-clocks[0])*1e-6*tmax/2 if len(clocks)>1 else 0,'epoch_count':len(epochs),'candidates':candidates,'default_candidate_cap':1000000,'strict_whole_bit_frequency_count':2*math.ceil(4*bound*f['bit_seconds'])+1,'channel':ch,'calendar':cal,'format':f}
print('GEOMETRY',json.dumps(report['geometry']),flush=True)
save('geometry.json',report['geometry'])
with threadpool_limits(limits=2):
    bank,report['bank_preparation']=timing(lambda:TiledAcquisition(m,templates,epochs,arrival_seconds=arrivals,frequencies_hz=freqs,clock_ppm=clocks,timing_tile_size=8,block_seconds=.25,coherence_seconds=(.5,math.inf),projection='core',execution='scan',device='cpu',precision='fp32',maximum_candidates=candidates,memory_limit_bytes=2*1024**3))
    report['frequency'],report['frequency_timing']=timing(lambda:estimate_dynamic_frequency(raw,m,freqs,valid=valid))
    report['geometry']['tile_count']=bank.tile_count;report['phase_guard']=bank.timing_guard
    report['projection']={'required_bands_hz':bank.required_bands_hz,'excluded_bands_hz':bank.excluded_bands_hz}
    def measure(label,size,pos,epoch,frequency,clock_ppm,threads=2):
        index=min(max(int(pos*(len(arrivals)-size)),0),len(arrivals)-size)
        tile=AcquisitionTile(int(epoch),tuple(arrivals[index:index+size]),float(frequency),float(clock_ppm))
        rx,prep=timing(lambda:bank._receiver(tile,valid,1536*1024**2))
        result,score=timing(lambda:rx.score(raw))
        row={'label':label,'tile':dict(epoch=tile.epoch,arrivals=tile.arrivals,frequency=tile.frequency,clock_ppm=tile.clock_ppm),'size':size,'threads':threads,'preparation':prep,'score':score,'work':rx.work(),'scan_backend':result.get('scan_backend'),'correlation_wall_seconds':result.get('correlation_seconds'),'state_wall_seconds':result.get('state_seconds'),'finite_scores':bool(np.isfinite(result['bit_scores']).all())}
        del result,rx;gc.collect();print('TILE',label,'prep',prep,'score',score,flush=True)
        return row
    training=[]
    for j,size in enumerate((1,4,8)):
        training.append(measure('fit-'+str(size),size,j/2,epochs[j*(len(epochs)-1)//2],(-bound,0,bound)[j],(-clock,0,clock)[j]))
    report['training']=training
    model={}
    for stage in ('preparation','score'):
        model[stage]={}
        for clocktype in ('cpu_seconds','wall_seconds'):
            x=np.array([r['size'] for r in training]);y=np.array([r[stage][clocktype] for r in training]);slope,intercept=np.polyfit(x,y,1)
            model[stage][clocktype]={'slope_per_candidate':float(slope),'intercept_per_tile':float(intercept),'size8_frozen':training[-1][stage][clocktype],'predicted_heldout_four_size8':4*training[-1][stage][clocktype]}
    model['frozen_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());save('frozen-stage-model.json',model)
    report['frozen_model']=model
    validation=[]
    for j,(pos,ep,fr,cl) in enumerate(((.17,epochs[0],0,clock),(.39,epochs[len(epochs)//2],-bound,0),(.63,epochs[-1],bound,-clock),(.87,epochs[0],-bound,clock))):
        validation.append(measure('heldout-'+str(j),8,pos,ep,fr,cl))
    report['heldout']=validation
    report['validation']={stage:{ct:{'predicted':model[stage][ct]['predicted_heldout_four_size8'],'observed':sum(r[stage][ct] for r in validation),'relative_error':model[stage][ct]['predicted_heldout_four_size8']/sum(r[stage][ct] for r in validation)-1} for ct in ('cpu_seconds','wall_seconds')}for stage in ('preparation','score')}
    save('intermediate.json',report)
    thread_rows=[]
    if time.perf_counter()-started<180:
        for n in (1,2,6):
            torch.set_num_threads(n)
            with threadpool_limits(limits=n):
                for rep in range(2):thread_rows.append(measure('thread-'+str(n)+'-'+str(rep),8,.5,epochs[len(epochs)//2],0,0,n))
    report['thread_comparison']=thread_rows
report['environment']={'platform':platform.platform(),'python':sys.version,'numpy':np.__version__,'scipy':scipy.__version__,'torch':torch.__version__,'threadpools':threadpool_info(),'cpu':subprocess.check_output(['lscpu'],text=True),'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_sha256':{str(p.relative_to(ROOT)):sha(p)for p in [ROOT/'tools/triad_dynamic_acquire.py',ROOT/'tools/triad_dynamic_joint.py',ROOT/'tools/triad_dynamic_variants.py',ROOT/'tools/triad_dynamic_scan.py',ROOT/'tools/triad_dynamic_frequency.py',ROOT/'build/dev-cli/triad_helper_export']},'input_sha256':{str(p.relative_to(OUT)):sha(p)for p in (OUT/'capture').iterdir()if p.is_file()}}
rows=training[-1:]+validation
report['conditional_full_bank_forecast']={'warning':'Tile scaling only. No outer-loop full bank validation; receipt memory and searches/coherence regimes unqualified. Mean full receiver capture duration used only as denominator.','tile_count':bank.tile_count,'score_cpu_seconds_range':[min(r['score']['cpu_seconds']for r in rows)*bank.tile_count,max(r['score']['cpu_seconds']for r in rows)*bank.tile_count],'preparation_cpu_seconds_range':[min(r['preparation']['cpu_seconds']for r in rows)*bank.tile_count,max(r['preparation']['cpu_seconds']for r in rows)*bank.tile_count],'score_wall_seconds_range':[min(r['score']['wall_seconds']for r in rows)*bank.tile_count,max(r['score']['wall_seconds']for r in rows)*bank.tile_count]}
report['wall_seconds_total']=time.perf_counter()-started;save('measurements.json',report)
print('DONE',json.dumps({'wall_seconds_total':report['wall_seconds_total'],'forecast':report['conditional_full_bank_forecast'],'validation':report['validation'],'frequency_status':report['frequency']['status'],'frequency_reason':report['frequency']['reason']}),flush=True)
